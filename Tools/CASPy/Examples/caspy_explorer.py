"""CASPy Explorer — standalone visual node editor (v2).

Run directly, no Jupyter:

    python caspy_explorer_server.py

KNOWN GAPS / VERIFY BEFORE TRUSTING OUTPUT:

  1. `ADSR`, `LFO`, `NoiseOscillatorWhite`, `NoiseOscillatorPink` are all
     missing `py::dynamic_attr()` in their pybind11 class declarations.
     add_node() sets a `_consumed` attribute after transferring ownership
     (see bind_graph.cpp) — without dynamic_attr() this raises
     `AttributeError: ... has no attribute '_consumed' and no __dict__`.
     Fix: add `py::dynamic_attr()` to bind_adsr.cpp, bind_lfo.cpp, and
     bind_noise.cpp's class_<> declarations (bind_oscillators.cpp and
     bind_filters.cpp already have it).

  2. ADSR's audio_in socket (added in this version so you can route an
     oscillator through an envelope) does nothing until caspi_Envelope.h
     is patched — the real ADSR class constructs its AudioNode base as
     `(0, 1)` (zero inputs). Until patched, connecting anything into it
     raises a clear GraphBuildError rather than silently doing nothing:

         ADSR()
             : Graph::AudioNode<ADSR<FloatType>, FloatType> (1, 1)  // was (0, 1)
         { }

         void processImpl (Graph::AudioContext<FloatType>& ctx) noexcept
         {
             auto& buf = this->outputBuffer;
             const auto F = buf.numFrames();
             const auto C = buf.numChannels();
             const auto* audioIn = ctx.getAudioInput (this->getId(), 0);  // nullptr if unconnected

             for (std::size_t f = 0; f < F; ++f)
             {
                 const FloatType env = render();
                 for (std::size_t ch = 0; ch < C; ++ch)
                 {
                     const FloatType carrier = (audioIn != nullptr) ? audioIn->sample (ch, f) : FloatType (1);
                     buf.sample (ch, f) = carrier * env;  // VCA when connected, raw envelope otherwise
                 }
             }
         }

     Same single output serves both roles: unconnected -> raw envelope
     (usable as a ModMatrix source), connected -> VCA'd audio.

  3. ModMatrix has no data-flow edge into the node it modulates — it
     writes through a raw ModulatableParameter pointer registered via
     register_parameter(), not through its own output buffer (always
     zeroed). AudioGraph's topological sort orders by connection edges
     only, so there's no confirmed ordering guarantee between a ModMatrix
     and the node whose parameter it drives. Treat modulation timing as
     diagnostic-grade until checked against caspi_Node.h's tie-breaking.

  4. Filter cutoff is not modulatable via ModMatrix: bind_filters.cpp
     exposes `cutoff` as a plain float property, not a
     Core::ModulatableParameter<float>& like BlepOscillator's `.frequency`.
     register_parameter() requires the latter. No cutoff_mod socket is
     offered on the Filter node — it would be a no-op.

  5. WaveTableBank1/4 aren't kept alive by pybind11 (no py::keep_alive<>()
     in bind_wavetable.cpp) — the bank must outlive the oscillator that
     references its table data. Held in a local list for the duration of
     build_and_render() to avoid a dangling reference.

  6. Parameter::set_base_normalised() takes a value normalised to the
     parameter's configured (min, max, scale) range, not the literal
     engineering-unit value (see bind_parameters.cpp). The Linear-scale
     inverse mapping used here is exact. Logarithmic is a best-effort
     inverse of a plausible denormalize formula — the real
     Parameter::value()/valueNormalised() implementation isn't shown in
     any file I have. Frequency avoids this entirely via the literal
     set_frequency(hz) setter instead.
"""

from __future__ import annotations

import base64
import io
import json
import math
import wave
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

import numpy as np
from scipy.signal import welch

try:
    import matplotlib
    matplotlib.use("Agg")  # headless — this is a server process, no display
    import matplotlib.pyplot as plt
except Exception:
    plt = None

try:
    import caspy
except Exception as exc:  # pragma: no cover - runtime dependency check
    caspy = None
    CASPY_IMPORT_ERROR = exc
else:
    CASPY_IMPORT_ERROR = None


# ---------------------------------------------------------------------------
# ModulatableParameter helpers
# ---------------------------------------------------------------------------

def _set_modparam_value(mod_param: Any, value: float) -> None:
    """Set a ModulatableParameter's base value from an engineering-unit value.

    set_base_normalised() expects a [0,1] fraction of the configured range
    (see bind_parameters.cpp), not the literal value. This inverts that
    mapping using the range/scale metadata Parameter exposes.

    Linear/Bipolar are exact-ish (identical formula). Logarithmic is
    best-effort — see gap #6 above.
    """
    lo = mod_param.get_min_value()
    hi = mod_param.get_max_value()
    scale = mod_param.get_scale()

    if hi == lo:
        norm = 0.0
    elif scale == caspy.ParameterScale.Logarithmic:
        lo_c = max(lo, 1e-9)
        value_c = max(value, lo_c)
        norm = (math.log(value_c) - math.log(lo_c)) / (math.log(hi) - math.log(lo_c))
    else:  # Linear or Bipolar
        norm = (value - lo) / (hi - lo)

    norm = max(0.0, min(1.0, norm))
    mod_param.set_base_normalised(norm)


# ---------------------------------------------------------------------------
# Backend: construct a real caspy.AudioGraph from a serialized patch
# ---------------------------------------------------------------------------

class GraphBuildError(RuntimeError):
    pass


def _construct_caspy_node(node: dict[str, Any], sample_rate: float,
                           wavetable_banks: list[Any]):
    node_type = node["type"]
    p = node["params"]

    if node_type == "Output":
        return None

    if node_type == "ModMatrix":
        return caspy.ModMatrix(4)  # fixed capacity; unused source slots are inert

    if node_type == "Oscillator":
        osc = caspy.oscillators.BlepOscillator()
        osc.set_sample_rate(float(sample_rate))
        osc.set_frequency(float(p.get("frequency", 440.0)))
        osc.set_shape(getattr(caspy.oscillators.WaveShape, p.get("waveform", "Saw")))
        _set_modparam_value(osc.amplitude, float(p.get("amplitude", 1.0)))
        _set_modparam_value(osc.pulse_width, float(p.get("pulse_width", 0.5)))
        return osc

    if node_type == "Wavetable":
        bank = caspy.wavetable.WaveTableBank1()
        waveform = p.get("waveform", "Sine")
        if waveform == "Sine":
            bank[0].fill_sine()
        elif waveform == "Saw":
            bank[0].fill_saw()
        else:
            bank[0].fill_triangle()
        wavetable_banks.append(bank)  # keep alive — see gap #5

        osc = caspy.wavetable.WavetableOscillator1(
            bank, float(sample_rate), float(p.get("frequency", 440.0))
        )
        osc.set_interpolation_mode(
            getattr(caspy.wavetable.InterpolationMode, p.get("interpolation", "Linear"))
        )
        _set_modparam_value(osc.amplitude, float(p.get("amplitude", 1.0)))
        return osc

    if node_type == "Noise":
        algo = p.get("algorithm", "White")
        osc = (caspy.noise.NoiseOscillatorWhite(float(sample_rate)) if algo == "White"
               else caspy.noise.NoiseOscillatorPink(float(sample_rate)))
        seed = p.get("seed")
        if seed not in (None, ""):
            osc.seed(int(seed))
        _set_modparam_value(osc.amplitude, float(p.get("amplitude", 1.0)))
        return osc

    if node_type == "LFO":
        lfo = caspy.lfo.LFO(float(sample_rate), float(p.get("rate", 2.0)))
        lfo.set_shape(getattr(caspy.lfo.LfoShape, p.get("shape", "Sine")))
        lfo.set_output_mode(getattr(caspy.lfo.LfoOutputMode, p.get("output_mode", "Bipolar")))
        return lfo

    if node_type == "ADSR":
        env = caspy.adsr.ADSR()
        env.set_sample_rate(float(sample_rate))
        env.set_attack_time(float(p.get("attack", 0.01)))
        env.set_decay_time(float(p.get("decay", 0.05)))
        env.set_sustain_level(float(p.get("sustain", 0.6)))
        env.set_release_time(float(p.get("release", 0.2)))
        return env

    if node_type == "Filter":
        filter_name = p.get("filter_name", "Biquad")
        cls = getattr(caspy, filter_name)
        cutoff = float(p.get("cutoff", 1200.0))
        q = float(p.get("q", 0.7))
        mode = getattr(caspy.FilterMode, p.get("mode", "LowPass"))

        if filter_name in ("Ladder", "DiodeLadder"):
            return cls(sample_rate, cutoff, q)
        if filter_name == "OnePole":
            return cls(sample_rate, cutoff, mode)
        if filter_name == "Biquad":
            return cls(sample_rate, cutoff, q, mode, float(p.get("gain", 0.0)))
        return cls(sample_rate, cutoff, q, mode)  # StateVariable

    raise ValueError(f"Unsupported node type: {node_type}")


def _resolve_output_target(nodes: list[dict[str, Any]],
                            connections: list[dict[str, Any]]) -> str:
    output_nodes = [n for n in nodes if n["type"] == "Output"]
    if not output_nodes:
        raise GraphBuildError("Add an Output node and connect something to it.")
    if len(output_nodes) > 1:
        raise GraphBuildError("Only one Output node is supported.")

    output_id = output_nodes[0]["id"]
    feeding = [c["source"] for c in connections if c["target"] == output_id]
    if not feeding:
        raise GraphBuildError("Connect a node to the Output node.")
    if len(feeding) > 1:
        raise GraphBuildError("Output node has more than one incoming connection.")
    return feeding[0]


def _apply_modmatrix_routings(
    nodes: list[dict[str, Any]],
    id_to_gid: dict[str, int],
    g: Any,
) -> None:
    """Parse ModMatrix routing widgets and wire them via register_parameter/add_routing.

    Routing format per slot: "<node name>.<param name>:<depth>[:<curve>]"
    e.g. "LFO 1.frequency:0.3:Linear" — routes source slot i (the audio
    connection wired into that ModMatrix's mod_in_i socket) into the named
    node's ModulatableParameter, scaled by depth.
    """
    name_to_id = {n.get("name", n["type"]): n["id"] for n in nodes}

    for node in nodes:
        if node["type"] != "ModMatrix":
            continue
        mm_gid = id_to_gid[node["id"]]
        mm_obj = g.get_node(mm_gid)

        for slot in range(4):
            raw = str(node["params"].get(f"route_{slot}", "")).strip()
            if not raw:
                continue

            try:
                target_name, rest = raw.split(".", 1)
                parts = rest.split(":")
                param_name = parts[0]
                depth = float(parts[1]) if len(parts) > 1 else 1.0
                curve_name = parts[2] if len(parts) > 2 else "Linear"
            except (ValueError, IndexError) as exc:
                raise GraphBuildError(
                    f"ModMatrix route_{slot} '{raw}' must be "
                    f"'<node name>.<param>:<depth>[:<curve>]'"
                ) from exc

            target_ui_id = name_to_id.get(target_name)
            if target_ui_id is None:
                raise GraphBuildError(f"ModMatrix route_{slot}: no node named '{target_name}'")
            target_gid = id_to_gid.get(target_ui_id)
            if target_gid is None:
                raise GraphBuildError(f"ModMatrix route_{slot}: target node not in graph")

            target_obj = g.get_node(target_gid)
            try:
                mod_param = getattr(target_obj, param_name)
            except AttributeError as exc:
                raise GraphBuildError(
                    f"ModMatrix route_{slot}: '{target_name}' has no attribute '{param_name}'"
                ) from exc

            try:
                dest_id = mm_obj.register_parameter(mod_param)
            except Exception as exc:
                raise GraphBuildError(
                    f"ModMatrix route_{slot}: register_parameter failed for "
                    f"'{target_name}.{param_name}' — it's probably not a "
                    f"ModulatableParameter ({exc})"
                ) from exc

            try:
                curve = getattr(caspy.ModulationCurve, curve_name)
            except AttributeError as exc:
                raise GraphBuildError(f"ModMatrix route_{slot}: unknown curve '{curve_name}'") from exc

            routing = caspy.ModulationRouting(source_id=slot, destination_id=dest_id, depth=depth)
            routing.curve = curve
            mm_obj.add_routing(routing)


def build_and_render(
    nodes: list[dict[str, Any]],
    connections: list[dict[str, Any]],
    sample_rate: float = 48000.0,
    frames: int = 512,
    duration_s: float = 2.0,
) -> dict[str, Any]:
    """Rebuild the graph from scratch and render duration_s seconds of audio.

    add_node() consumes the Python object (bind_graph.cpp: raw pointer
    recovered, wrapped in unique_ptr, ownership transferred to the graph),
    so there's no way to reuse constructed nodes across rebuilds. Rebuilding
    per render is the simplest correct pattern.
    """
    if caspy is None:
        raise RuntimeError(f"caspy is not importable: {CASPY_IMPORT_ERROR}")

    num_blocks = max(1, math.ceil(duration_s * sample_rate / frames))

    output_source_id = _resolve_output_target(nodes, connections)

    g = caspy.AudioGraph()
    id_to_gid: dict[str, int] = {}
    adsr_gids: list[tuple[int, dict[str, Any]]] = []
    wavetable_banks: list[Any] = []

    real_nodes = [n for n in nodes if n["type"] != "Output"]
    for node in real_nodes:
        obj = _construct_caspy_node(node, sample_rate, wavetable_banks)
        gid = g.add_node(obj)
        id_to_gid[node["id"]] = gid
        if node["type"] == "ADSR":
            adsr_gids.append((gid, node))

    id_to_node = {n["id"]: n for n in real_nodes}

    real_connections = [
        c for c in connections
        if c["target"] in id_to_gid and c["source"] in id_to_gid
    ]
    for conn in real_connections:
        src = id_to_gid[conn["source"]]
        dst = id_to_gid[conn["target"]]
        src_port = conn.get("src_port", 0)
        dst_port = conn.get("dst_port", 0)
        try:
            g.connect(src, src_port, dst, dst_port)
        except ValueError as exc:
            dst_node = id_to_node.get(conn["target"])
            if dst_node is not None and dst_node["type"] == "ADSR" and dst_port == 0:
                raise GraphBuildError(
                    f"{conn['source']} -> {conn['target']}: ADSR has no audio input port in "
                    f"the compiled caspy build yet ({exc}). Apply the caspi_Envelope.h patch "
                    f"in this file's docstring (gap #2) and rebuild caspy first."
                ) from exc
            raise GraphBuildError(f"{conn['source']} -> {conn['target']}: {exc}") from exc

    _apply_modmatrix_routings(nodes, id_to_gid, g)

    out_gid = id_to_gid[output_source_id]

    try:
        g.prepare(1, frames, sample_rate)
    except ValueError as exc:
        raise GraphBuildError(str(exc)) from exc

    if not adsr_gids:
        audio = g.render(out_gid, num_blocks=num_blocks, channels=1,
                          frames=frames, sample_rate=sample_rate)
        signal = audio[0]
    else:
        for gid, _ in adsr_gids:
            g.get_node(gid).note_on()

        blocks = []
        for i in range(num_blocks):
            for gid, node in adsr_gids:
                off_block = int(node["params"].get("note_off_fraction", 0.5) * num_blocks)
                if i == off_block:
                    g.get_node(gid).note_off()
            g.process()
            blocks.append(g.get_node(out_gid).get_output_buffer(0).copy())
        signal = np.concatenate(blocks, axis=1)[0]

    peak = float(np.max(np.abs(signal))) if signal.size else 0.0
    if peak > 0.0:
        signal = signal / peak

    return {
        "signal": signal.astype(np.float32),
        "sample_rate": float(sample_rate),
        "graph": g,
        "id_map": id_to_gid,
        "sorted_order": g.get_sorted_order(),
    }


def render_graph_text(nodes, connections, result=None) -> str:
    lines = ["Nodes:"]
    for n in nodes:
        gid = result["id_map"].get(n["id"], "-") if result else "-"
        lines.append(f"  [{gid}] {n.get('name', n['type'])} ({n['type']})")
    lines.append("")
    lines.append("Connections:")
    if not connections:
        lines.append("  (none)")
    id_to_node = {n["id"]: n for n in nodes}
    for c in connections:
        src, dst = id_to_node.get(c["source"]), id_to_node.get(c["target"])
        if src is None or dst is None:
            continue
        lines.append(
            f"  {src.get('name', src['type'])}:{c.get('src_port', 0)} --> "
            f"{dst.get('name', dst['type'])}:{c.get('dst_port', 0)}"
        )
    mm_nodes = [n for n in nodes if n["type"] == "ModMatrix"]
    if mm_nodes:
        lines.append("")
        lines.append("Modulation routings:")
        for n in mm_nodes:
            for slot in range(4):
                raw = str(n["params"].get(f"route_{slot}", "")).strip()
                if raw:
                    lines.append(f"  {n.get('name', 'ModMatrix')} slot {slot}: {raw}")
    if result:
        lines.append("")
        lines.append(f"Topological order (graph node ids): {result['sorted_order']}")
        lines.append("(ModMatrix ordering vs. its modulation targets is unverified — see gap #3)")
    return "\n".join(lines)


def _signal_to_wav_bytes(signal: np.ndarray, sample_rate: float) -> bytes:
    pcm = np.clip(signal, -1.0, 1.0)
    pcm16 = (pcm * 32767.0).astype(np.int16)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(int(sample_rate))
        wf.writeframes(pcm16.tobytes())
    return buf.getvalue()


def _make_plot_png(signal: np.ndarray, sample_rate: float) -> bytes:
    if plt is None:
        return b""
    f_psd, psd = welch(signal, fs=sample_rate, nperseg=2048)
    fig, axes = plt.subplots(1, 2, figsize=(12, 3.5))

    t_ms = np.arange(min(4410, signal.size)) / sample_rate * 1000
    axes[0].plot(t_ms, signal[:4410], linewidth=0.6, color="#1f77b4")
    axes[0].set_xlabel("Time (ms)")
    axes[0].set_ylabel("Amplitude")
    axes[0].set_title("Waveform (first 100ms)")
    axes[0].set_ylim(-1.2, 1.2)
    axes[0].grid(True, alpha=0.3)

    axes[1].semilogx(f_psd[1:], 10 * np.log10(psd[1:] + 1e-30), linewidth=0.7, color="#ff7f0e")
    axes[1].set_xlabel("Frequency (Hz)")
    axes[1].set_ylabel("PSD (dB/Hz)")
    axes[1].set_title("Output spectrum")
    axes[1].set_xlim(20, sample_rate / 2)
    axes[1].grid(True, which="both", alpha=0.3)

    plt.tight_layout()
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=100)
    plt.close(fig)
    return buf.getvalue()


# ---------------------------------------------------------------------------
# Frontend: litegraph.js canvas, served as a static page
# ---------------------------------------------------------------------------

_HTML = r"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>CASPy Explorer</title>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/litegraph.js/css/litegraph.css">
<script src="https://cdn.jsdelivr.net/npm/litegraph.js/build/litegraph.min.js"></script>
<style>
  body { font-family: sans-serif; margin: 20px; }
  #lg_summary { background: #f5f5f5; padding: 8px; font-size: 12px; white-space: pre-wrap; }
  #lg_plot { max-width: 100%; display: none; }
  #lg_audio { display: none; margin-top: 8px; }
  .toolbar label { margin-left: 14px; font-size: 13px; }
</style>
</head>
<body>
  <h2>CASPy Explorer</h2>
  <div class="toolbar" style="margin-bottom: 8px;">
    <button id="lg_add_osc">+ Oscillator</button>
    <button id="lg_add_wavetable">+ Wavetable</button>
    <button id="lg_add_noise">+ Noise</button>
    <button id="lg_add_lfo">+ LFO</button>
    <button id="lg_add_adsr">+ ADSR</button>
    <button id="lg_add_filter">+ Filter</button>
    <button id="lg_add_modmatrix">+ ModMatrix</button>
    <button id="lg_add_output">+ Output</button>
    <label>Duration (s): <input id="lg_duration" type="number" value="2.0" min="0.1" max="30" step="0.1" style="width:60px;"></label>
    <label>Sample rate: <input id="lg_samplerate" type="number" value="48000" min="8000" max="192000" step="1000" style="width:80px;"></label>
    <button id="lg_render" style="margin-left: 20px; background:#0066cc; color:white;">Render</button>
    <span id="lg_status" style="margin-left: 10px; color: #666; font-size: 12px;"></span>
  </div>
  <canvas id="caspy_canvas" width="1000" height="520" style="border:1px solid #ccc;"></canvas>
  <div style="font-size: 12px; color: #666; margin-top: 6px;">
    Drag from a socket to connect. ADSR's <code>audio_in</code> requires a C++-side patch
    (see the server script's docstring, gap #2) — until applied, wiring anything into it
    produces a clear error rather than doing nothing silently. ModMatrix routing slots use
    <code>&lt;node name&gt;.&lt;param&gt;:&lt;depth&gt;[:&lt;curve&gt;]</code>, e.g.
    <code>Oscillator 1.frequency:0.3:Linear</code> — the source is whatever's wired into that
    slot's audio input. Filter cutoff modulation isn't wired up on the C++ side yet (gap #4).
  </div>

  <div id="lg_output" style="margin-top: 16px;">
    <pre id="lg_summary"></pre>
    <img id="lg_plot">
    <audio id="lg_audio" controls></audio>
  </div>

<script>
function CaspyOscillator() {
    this.addOutput("audio", "audio");
    this.properties = { frequency: 440, waveform: "Saw", amplitude: 1.0, pulse_width: 0.5 };
    this.addWidget("combo", "waveform", this.properties.waveform,
        (v) => { this.properties.waveform = v; }, { values: ["Sine", "Saw", "Square", "Triangle", "Pulse"] });
    this.addWidget("number", "frequency", this.properties.frequency,
        (v) => { this.properties.frequency = v; }, { min: 20, max: 5000 });
    this.addWidget("number", "amplitude", this.properties.amplitude,
        (v) => { this.properties.amplitude = v; }, { min: 0, max: 1 });
    this.addWidget("number", "pulse_width", this.properties.pulse_width,
        (v) => { this.properties.pulse_width = v; }, { min: 0.01, max: 0.99 });
    this.size = [200, 130];
}
CaspyOscillator.title = "Oscillator";
LiteGraph.registerNodeType("caspy/oscillator", CaspyOscillator);

function CaspyWavetable() {
    this.addOutput("audio", "audio");
    this.properties = { frequency: 440, waveform: "Sine", amplitude: 1.0, interpolation: "Linear" };
    this.addWidget("combo", "waveform", this.properties.waveform,
        (v) => { this.properties.waveform = v; }, { values: ["Sine", "Saw", "Triangle"] });
    this.addWidget("number", "frequency", this.properties.frequency,
        (v) => { this.properties.frequency = v; }, { min: 20, max: 5000 });
    this.addWidget("number", "amplitude", this.properties.amplitude,
        (v) => { this.properties.amplitude = v; }, { min: 0, max: 1 });
    this.addWidget("combo", "interpolation", this.properties.interpolation,
        (v) => { this.properties.interpolation = v; }, { values: ["Linear", "Hermite"] });
    this.size = [200, 140];
}
CaspyWavetable.title = "Wavetable";
LiteGraph.registerNodeType("caspy/wavetable", CaspyWavetable);

function CaspyNoise() {
    this.addOutput("audio", "audio");
    this.properties = { algorithm: "White", amplitude: 1.0, seed: 0 };
    this.addWidget("combo", "algorithm", this.properties.algorithm,
        (v) => { this.properties.algorithm = v; }, { values: ["White", "Pink"] });
    this.addWidget("number", "amplitude", this.properties.amplitude,
        (v) => { this.properties.amplitude = v; }, { min: 0, max: 1 });
    this.addWidget("number", "seed", this.properties.seed,
        (v) => { this.properties.seed = v; }, { min: 0, max: 999999, step: 1 });
    this.size = [180, 120];
}
CaspyNoise.title = "Noise";
LiteGraph.registerNodeType("caspy/noise", CaspyNoise);

function CaspyLFO() {
    this.addOutput("audio", "audio");
    this.properties = { rate: 2.0, shape: "Sine", output_mode: "Bipolar" };
    this.addWidget("combo", "shape", this.properties.shape,
        (v) => { this.properties.shape = v; },
        { values: ["Sine", "Triangle", "Saw", "ReverseSaw", "Square"] });
    this.addWidget("number", "rate", this.properties.rate,
        (v) => { this.properties.rate = v; }, { min: 0.01, max: 20 });
    this.addWidget("combo", "output_mode", this.properties.output_mode,
        (v) => { this.properties.output_mode = v; }, { values: ["Bipolar", "Unipolar"] });
    this.size = [180, 100];
}
CaspyLFO.title = "LFO";
LiteGraph.registerNodeType("caspy/lfo", CaspyLFO);

function CaspyADSR() {
    this.addInput("audio_in", "audio");   // VCA carrier — requires C++ patch, see gap #2
    this.addOutput("env_or_vca", "audio");
    this.properties = { attack: 0.01, decay: 0.05, sustain: 0.6, release: 0.2, note_off_fraction: 0.5 };
    this.addWidget("number", "attack", this.properties.attack, (v) => { this.properties.attack = v; }, { min: 0.001, max: 0.5 });
    this.addWidget("number", "decay", this.properties.decay, (v) => { this.properties.decay = v; }, { min: 0.001, max: 1.0 });
    this.addWidget("number", "sustain", this.properties.sustain, (v) => { this.properties.sustain = v; }, { min: 0, max: 1 });
    this.addWidget("number", "release", this.properties.release, (v) => { this.properties.release = v; }, { min: 0.001, max: 2.0 });
    this.addWidget("number", "note_off_fraction", this.properties.note_off_fraction, (v) => { this.properties.note_off_fraction = v; }, { min: 0, max: 1 });
    this.size = [220, 170];
}
CaspyADSR.title = "ADSR";
LiteGraph.registerNodeType("caspy/adsr", CaspyADSR);

function CaspyFilter() {
    this.addInput("audio_in", "audio");
    this.addOutput("audio_out", "audio");
    this.properties = { filter_name: "Biquad", cutoff: 1200, q: 0.7, mode: "LowPass", gain: 0 };
    this.addWidget("combo", "filter_name", this.properties.filter_name,
        (v) => { this.properties.filter_name = v; },
        { values: ["StateVariable", "Biquad", "Ladder", "DiodeLadder", "OnePole"] });
    this.addWidget("number", "cutoff", this.properties.cutoff, (v) => { this.properties.cutoff = v; }, { min: 20, max: 20000 });
    this.addWidget("number", "q", this.properties.q, (v) => { this.properties.q = v; }, { min: 0.1, max: 20 });
    this.addWidget("combo", "mode", this.properties.mode,
        (v) => { this.properties.mode = v; },
        { values: ["LowPass", "HighPass", "BandPass", "Notch", "Peak", "AllPass", "LowShelf", "HighShelf"] });
    this.addWidget("number", "gain", this.properties.gain, (v) => { this.properties.gain = v; }, { min: -24, max: 24 });
    this.size = [200, 170];
}
CaspyFilter.title = "Filter";
LiteGraph.registerNodeType("caspy/filter", CaspyFilter);

function CaspyModMatrix() {
    for (let i = 0; i < 4; i++) {
        this.addInput("mod_in_" + i, "audio");
    }
    this.properties = { route_0: "", route_1: "", route_2: "", route_3: "" };
    for (let i = 0; i < 4; i++) {
        this.addWidget("text", "route_" + i, "",
            ((idx) => (v) => { this.properties["route_" + idx] = v; })(i));
    }
    this.size = [240, 170];
}
CaspyModMatrix.title = "ModMatrix";
LiteGraph.registerNodeType("caspy/modmatrix", CaspyModMatrix);

function CaspyOutput() {
    this.addInput("audio_in", "audio");
    this.properties = {};
    this.size = [120, 40];
}
CaspyOutput.title = "Output";
LiteGraph.registerNodeType("caspy/output", CaspyOutput);

const TYPE_MAP = {
    "caspy/oscillator": "Oscillator",
    "caspy/wavetable": "Wavetable",
    "caspy/noise": "Noise",
    "caspy/lfo": "LFO",
    "caspy/adsr": "ADSR",
    "caspy/filter": "Filter",
    "caspy/modmatrix": "ModMatrix",
    "caspy/output": "Output",
};

const graph = new LGraph();
const canvas = new LGraphCanvas("#caspy_canvas", graph);
graph.start();

function addNode(type) {
    const node = LiteGraph.createNode(type);
    node.pos = [50 + Math.random() * 600, 50 + Math.random() * 350];
    graph.add(node);
}

document.getElementById("lg_add_osc").onclick = () => addNode("caspy/oscillator");
document.getElementById("lg_add_wavetable").onclick = () => addNode("caspy/wavetable");
document.getElementById("lg_add_noise").onclick = () => addNode("caspy/noise");
document.getElementById("lg_add_lfo").onclick = () => addNode("caspy/lfo");
document.getElementById("lg_add_adsr").onclick = () => addNode("caspy/adsr");
document.getElementById("lg_add_filter").onclick = () => addNode("caspy/filter");
document.getElementById("lg_add_modmatrix").onclick = () => addNode("caspy/modmatrix");
document.getElementById("lg_add_output").onclick = () => addNode("caspy/output");

document.getElementById("lg_render").onclick = async () => {
    const nodes = graph._nodes.map(n => ({
        id: String(n.id),
        type: TYPE_MAP[n.type],
        name: n.title || TYPE_MAP[n.type],
        params: n.properties,
    }));
    const connections = Object.values(graph.links || {}).map(link => ({
        source: String(link.origin_id),
        target: String(link.target_id),
        src_port: link.origin_slot,
        dst_port: link.target_slot,
    }));
    const payload = {
        nodes: nodes,
        connections: connections,
        sample_rate: parseFloat(document.getElementById("lg_samplerate").value) || 48000,
        duration_s: parseFloat(document.getElementById("lg_duration").value) || 2.0,
    };

    const statusEl = document.getElementById("lg_status");
    const summaryEl = document.getElementById("lg_summary");
    const plotEl = document.getElementById("lg_plot");
    const audioEl = document.getElementById("lg_audio");

    statusEl.textContent = "Rendering...";
    try {
        const resp = await fetch("/render", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
        });
        const data = await resp.json();

        if (data.error) {
            summaryEl.textContent = "Render failed: " + data.error;
            plotEl.style.display = "none";
            audioEl.style.display = "none";
        } else {
            summaryEl.textContent = data.summary;
            if (data.plot_png_b64) {
                plotEl.src = "data:image/png;base64," + data.plot_png_b64;
                plotEl.style.display = "block";
            }
            audioEl.src = "data:audio/wav;base64," + data.audio_wav_b64;
            audioEl.style.display = "block";
        }
    } catch (err) {
        summaryEl.textContent = "Request failed: " + err;
    }
    statusEl.textContent = "";
};
</script>
</body>
</html>
"""


# ---------------------------------------------------------------------------
# HTTP server
# ---------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    def _send_json(self, obj: dict[str, Any], status: int = 200) -> None:
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path in ("/", "/index.html"):
            body = _HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self) -> None:
        if self.path != "/render":
            self.send_response(404)
            self.end_headers()
            return

        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length)

        try:
            payload = json.loads(raw.decode("utf-8"))
            nodes = payload["nodes"]
            connections = payload["connections"]
            sample_rate = float(payload.get("sample_rate", 48000.0))
            duration_s = float(payload.get("duration_s", 2.0))
            result = build_and_render(nodes, connections, sample_rate=sample_rate,
                                       duration_s=duration_s)
        except GraphBuildError as exc:
            self._send_json({"error": str(exc)})
            return
        except Exception as exc:
            self._send_json({"error": f"{type(exc).__name__}: {exc}"})
            return

        signal = result["signal"]
        sr = result["sample_rate"]
        summary = render_graph_text(nodes, connections, result)
        png_bytes = _make_plot_png(signal, sr)
        wav_bytes = _signal_to_wav_bytes(signal, sr)

        self._send_json({
            "summary": summary,
            "plot_png_b64": base64.b64encode(png_bytes).decode("ascii") if png_bytes else None,
            "audio_wav_b64": base64.b64encode(wav_bytes).decode("ascii"),
        })

    def log_message(self, format: str, *args: Any) -> None:
        pass  # quiet by default; comment out this override to debug requests


def main() -> None:
    port = 8765
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    url = f"http://127.0.0.1:{port}/"
    print(f"CASPy Explorer running at {url}")
    if caspy is None:
        print(f"WARNING: caspy is not importable ({CASPY_IMPORT_ERROR}) — rendering will fail.")
    webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()