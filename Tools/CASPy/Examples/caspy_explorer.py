"""CASPy Explorer — standalone visual node editor (v3).

Run directly, no Jupyter:

    python caspy_explorer_server.py

WHAT CHANGED FROM v2:

  - ModMatrix is no longer a user-facing node. Every modulatable parameter
    (frequency, amplitude, pulse_width, morph_position) gets its own CV
    input socket directly on the node, drawn like any other cable, with an
    attenuverter (depth [-1,1] + curve) widget pair right next to the
    socket. The backend auto-synthesizes one caspy.ModMatrix per render by
    scanning connections whose destination slot name ends in "_mod" —
    same underlying register_parameter()/add_routing() mechanism as v2,
    just no longer something you build by hand.
  - Wavetable nodes support 1-table or 4-table (morph) banks.
  - SyncedOscillatorPair is a new composite node. See gap #3 below for
    why it's heavily restricted.

KNOWN GAPS / VERIFY BEFORE TRUSTING OUTPUT:

  1. `ADSR`, `LFO`, `NoiseOscillatorWhite`, `NoiseOscillatorPink` are all
     missing `py::dynamic_attr()` in their pybind11 class declarations.
     add_node() sets a `_consumed` attribute after transferring ownership
     (see bind_graph.cpp) — without dynamic_attr() this raises
     `AttributeError: ... has no attribute '_consumed' and no __dict__`.
     Fix: add `py::dynamic_attr()` to bind_adsr.cpp, bind_lfo.cpp, and
     bind_noise.cpp's class_<> declarations.

  2. ADSR's audio_in socket does nothing until caspi_Envelope.h is
     patched — the real ADSR class constructs its AudioNode base as
     `(0, 1)` (zero inputs). Until patched, connecting anything into it
     raises a clear GraphBuildError:

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
                     buf.sample (ch, f) = carrier * env;
                 }
             }
         }

  3. SyncedOscillatorPair does NOT run through AudioGraph at all.
     bind_wavetable.cpp's render_hard_sync() is a free function that
     manually loops two raw WavetableOscillator objects outside the graph
     entirely (primary.render_sample() -> check phase_wrapped() ->
     secondary.force_sync()) — there's no sync_in port on any AudioNode.
     Consequence: if a SyncedOscillatorPair is present, it must be the
     ONLY real node besides Output, wired directly to it. No Filter, no
     ADSR, nothing else in the patch. This is enforced, not just
     documented — build_and_render raises GraphBuildError otherwise.
     The real fix is a C++ sync_in control port read once per sample
     inside the oscillator's own processImpl; I don't have enough of the
     AudioNode/NodeBase header in front of me to spec that confidently.

  4. ModMatrix has no data-flow edge into the node it modulates — it
     writes through a raw ModulatableParameter pointer, not through its
     own output buffer (always zeroed). AudioGraph's topological sort
     orders by connection edges only, so there's no confirmed ordering
     guarantee between the auto-synthesized ModMatrix and the node whose
     parameter it drives. Treat modulation timing as diagnostic-grade
     until checked against caspi_Node.h's tie-breaking rule.

  5. Filter cutoff is not modulatable: bind_filters.cpp exposes `cutoff`
     as a plain float property, not a Core::ModulatableParameter<float>&.
     register_parameter() requires the latter. No cutoff_mod socket is
     offered — it would be a no-op.

  6. WaveTableBank1/4 aren't kept alive by pybind11 (no py::keep_alive<>()
     in bind_wavetable.cpp). Held in a local list for the duration of
     build_and_render() to avoid a dangling reference.

  7. Parameter::set_base_normalised() takes a value normalised to the
     parameter's configured (min, max, scale) range, not the literal
     engineering-unit value. The Linear-scale inverse mapping used here
     is exact. Logarithmic is a best-effort inverse of a plausible
     denormalize formula — the real Parameter denormalize implementation
     isn't shown in any file I have. Frequency avoids this via the
     literal set_frequency(hz) setter instead. morph_position is passed
     straight through as [0,1] per its own docstring ("normalised [0,1],
     maps to [0,3] at render time") — no extra conversion applied here.
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


CV_SLOT_TO_PARAM = {
    "freq_mod": "frequency",
    "amp_mod": "amplitude",
    "pulse_width_mod": "pulse_width",
    "morph_mod": "morph_position",
}


# ---------------------------------------------------------------------------
# ModulatableParameter helpers
# ---------------------------------------------------------------------------

def _set_modparam_value(mod_param: Any, value: float) -> None:
    """Set a ModulatableParameter's base value from an engineering-unit value.

    set_base_normalised() expects a [0,1] fraction of the configured range,
    not the literal value (see bind_parameters.cpp). Linear/Bipolar are
    exact-ish (identical formula here). Logarithmic is best-effort — see
    gap #7 above.
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


def _fill_bank_table(table: Any, waveform: str) -> None:
    if waveform == "Sine":
        table.fill_sine()
    elif waveform == "Saw":
        table.fill_saw()
    else:
        table.fill_triangle()


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

    if node_type == "Oscillator":
        osc = caspy.oscillators.BlepOscillator()
        osc.set_sample_rate(float(sample_rate))
        osc.set_frequency(float(p.get("frequency", 440.0)))
        osc.set_shape(getattr(caspy.oscillators.WaveShape, p.get("waveform", "Saw")))
        _set_modparam_value(osc.amplitude, float(p.get("amplitude", 1.0)))
        _set_modparam_value(osc.pulse_width, float(p.get("pulse_width", 0.5)))
        return osc

    if node_type == "Wavetable":
        table_count = str(p.get("table_count", "1"))
        interp = getattr(caspy.wavetable.InterpolationMode, p.get("interpolation", "Linear"))
        freq = float(p.get("frequency", 440.0))

        if table_count == "4":
            bank = caspy.wavetable.WaveTableBank4()
            for i in range(4):
                _fill_bank_table(bank[i], p.get(f"table_{i}", "Sine"))
            wavetable_banks.append(bank)  # keep alive — see gap #6
            osc = caspy.wavetable.WavetableOscillator4(bank, float(sample_rate), freq)
            _set_modparam_value(osc.morph_position, float(p.get("morph_position", 0.0)))
        else:
            bank = caspy.wavetable.WaveTableBank1()
            _fill_bank_table(bank[0], p.get("table_0", "Sine"))
            wavetable_banks.append(bank)
            osc = caspy.wavetable.WavetableOscillator1(bank, float(sample_rate), freq)

        osc.set_interpolation_mode(interp)
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


# ---------------------------------------------------------------------------
# SyncedOscillatorPair — bypasses AudioGraph entirely (see gap #3)
# ---------------------------------------------------------------------------

def _render_synced_pair(
    nodes: list[dict[str, Any]],
    connections: list[dict[str, Any]],
    sample_rate: float,
    duration_s: float,
) -> dict[str, Any]:
    if caspy is None:
        raise RuntimeError(f"caspy is not importable: {CASPY_IMPORT_ERROR}")

    synced_nodes = [n for n in nodes if n["type"] == "SyncedOscillatorPair"]
    if len(synced_nodes) > 1:
        raise GraphBuildError("Only one SyncedOscillatorPair is supported per patch.")

    other_real = [n for n in nodes if n["type"] not in ("SyncedOscillatorPair", "Output")]
    if other_real:
        raise GraphBuildError(
            "SyncedOscillatorPair bypasses AudioGraph entirely (render_hard_sync() runs "
            "outside the graph, see gap #3) — it must be the only node besides Output. "
            "Remove other nodes or don't use SyncedOscillatorPair."
        )

    output_source_id = _resolve_output_target(nodes, connections)
    sync_node = synced_nodes[0]
    if output_source_id != sync_node["id"]:
        raise GraphBuildError("Output must be connected directly to the SyncedOscillatorPair node.")

    p = sync_node["params"]
    num_samples = max(1, int(round(duration_s * sample_rate)))

    primary_bank = caspy.wavetable.WaveTableBank1()
    _fill_bank_table(primary_bank[0], p.get("primary_waveform", "Sine"))
    primary = caspy.wavetable.WavetableOscillator1(
        primary_bank, float(sample_rate), float(p.get("primary_frequency", 220.0))
    )

    secondary_bank = caspy.wavetable.WaveTableBank1()
    _fill_bank_table(secondary_bank[0], p.get("secondary_waveform", "Saw"))
    secondary = caspy.wavetable.WavetableOscillator1(
        secondary_bank, float(sample_rate), float(p.get("secondary_frequency", 220.0))
    )

    signal, sync_indices = caspy.wavetable.render_hard_sync(primary, secondary, num_samples)
    signal = np.asarray(signal, dtype=np.float32)

    peak = float(np.max(np.abs(signal))) if signal.size else 0.0
    if peak > 0.0:
        signal = signal / peak

    return {
        "signal": signal,
        "sample_rate": float(sample_rate),
        "graph": None,
        "id_map": {sync_node["id"]: -1},
        "sorted_order": None,
        "sync_indices": list(sync_indices),
    }


# ---------------------------------------------------------------------------
# Main build/render path
# ---------------------------------------------------------------------------

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

    CV connections (destination slot name ending "_mod") are diverted into
    an auto-synthesized caspy.ModMatrix rather than g.connect()'d directly
    — see the module docstring for why.
    """
    if caspy is None:
        raise RuntimeError(f"caspy is not importable: {CASPY_IMPORT_ERROR}")

    if any(n["type"] == "SyncedOscillatorPair" for n in nodes):
        return _render_synced_pair(nodes, connections, sample_rate, duration_s)

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

    id_to_node = {n["id"]: n for n in nodes}

    audio_conns = []
    cv_conns = []
    for c in connections:
        if c["target"] not in id_to_gid or c["source"] not in id_to_gid:
            continue
        if str(c.get("dst_slot_name") or "").endswith("_mod"):
            cv_conns.append(c)
        else:
            audio_conns.append(c)

    for conn in audio_conns:
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
                    f"the compiled caspy build yet ({exc}). See gap #2."
                ) from exc
            raise GraphBuildError(f"{conn['source']} -> {conn['target']}: {exc}") from exc

    if cv_conns:
        mm = caspy.ModMatrix(len(cv_conns))
        mm_gid = g.add_node(mm)

        for i, c in enumerate(cv_conns):
            src_gid = id_to_gid[c["source"]]
            try:
                g.connect(src_gid, c.get("src_port", 0), mm_gid, i)
            except ValueError as exc:
                raise GraphBuildError(f"CV connection {c['source']} -> {c['target']}: {exc}") from exc

        mm_obj = g.get_node(mm_gid)
        for i, c in enumerate(cv_conns):
            dst_node = id_to_node[c["target"]]
            dst_gid = id_to_gid[c["target"]]
            slot_name = c.get("dst_slot_name")
            param_name = CV_SLOT_TO_PARAM.get(slot_name)
            if param_name is None:
                raise GraphBuildError(f"Unknown CV destination slot '{slot_name}'")

            depth = float(dst_node["params"].get(f"{slot_name}_depth", 1.0))
            curve_name = dst_node["params"].get(f"{slot_name}_curve", "Linear")

            target_obj = g.get_node(dst_gid)
            try:
                mod_param = getattr(target_obj, param_name)
            except AttributeError as exc:
                raise GraphBuildError(
                    f"CV target '{dst_node.get('name')}' has no attribute '{param_name}'"
                ) from exc

            try:
                dest_id = mm_obj.register_parameter(mod_param)
            except Exception as exc:
                raise GraphBuildError(
                    f"register_parameter failed for '{dst_node.get('name')}.{param_name}' — "
                    f"it's probably not a ModulatableParameter ({exc})"
                ) from exc

            try:
                curve = getattr(caspy.ModulationCurve, curve_name)
            except AttributeError as exc:
                raise GraphBuildError(f"Unknown modulation curve '{curve_name}'") from exc

            routing = caspy.ModulationRouting(source_id=i, destination_id=dest_id, depth=depth)
            routing.curve = curve
            mm_obj.add_routing(routing)

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
        "sync_indices": None,
    }


def render_graph_text(nodes, connections, result=None) -> str:
    lines = ["Nodes:"]
    for n in nodes:
        gid = result["id_map"].get(n["id"], "-") if result else "-"
        lines.append(f"  [{gid}] {n.get('name', n['type'])} ({n['type']})")
    lines.append("")
    lines.append("Audio connections:")
    audio_lines = 0
    for c in connections:
        if str(c.get("dst_slot_name") or "").endswith("_mod"):
            continue
        id_to_node = {n["id"]: n for n in nodes}
        src, dst = id_to_node.get(c["source"]), id_to_node.get(c["target"])
        if src is None or dst is None:
            continue
        lines.append(f"  {src.get('name', src['type'])} --> {dst.get('name', dst['type'])}")
        audio_lines += 1
    if audio_lines == 0:
        lines.append("  (none)")

    lines.append("")
    lines.append("Modulation (CV) connections:")
    id_to_node = {n["id"]: n for n in nodes}
    cv_lines = 0
    for c in connections:
        slot_name = str(c.get("dst_slot_name") or "")
        if not slot_name.endswith("_mod"):
            continue
        src, dst = id_to_node.get(c["source"]), id_to_node.get(c["target"])
        if src is None or dst is None:
            continue
        depth = dst["params"].get(f"{slot_name}_depth", 1.0)
        curve = dst["params"].get(f"{slot_name}_curve", "Linear")
        param_name = CV_SLOT_TO_PARAM.get(slot_name, slot_name)
        lines.append(
            f"  {src.get('name', src['type'])} ~~> {dst.get('name', dst['type'])}.{param_name} "
            f"(depth={depth}, curve={curve})"
        )
        cv_lines += 1
    if cv_lines == 0:
        lines.append("  (none)")

    if result:
        if result.get("sorted_order") is not None:
            lines.append("")
            lines.append(f"Topological order (graph node ids): {result['sorted_order']}")
            lines.append("(ModMatrix ordering vs. its modulation targets is unverified — see gap #4)")
        if result.get("sync_indices") is not None:
            n_sync = len(result["sync_indices"])
            preview = result["sync_indices"][:10]
            lines.append("")
            lines.append(f"Sync events: {n_sync} (first 10 sample indices: {preview})")
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
    <button id="lg_add_syncpair">+ Synced Pair</button>
    <button id="lg_add_output">+ Output</button>
    <label>Duration (s): <input id="lg_duration" type="number" value="2.0" min="0.1" max="30" step="0.1" style="width:60px;"></label>
    <label>Sample rate: <input id="lg_samplerate" type="number" value="48000" min="8000" max="192000" step="1000" style="width:80px;"></label>
    <button id="lg_render" style="margin-left: 20px; background:#0066cc; color:white;">Render</button>
    <span id="lg_status" style="margin-left: 10px; color: #666; font-size: 12px;"></span>
  </div>
  <canvas id="caspy_canvas" width="1100" height="560" style="border:1px solid #ccc;"></canvas>
  <div style="font-size: 12px; color: #666; margin-top: 6px;">
    Every modulatable parameter has its own <code>_mod</code> input socket with a depth
    [-1,1] and curve widget right below it — wire an LFO/ADSR/Noise output into it directly,
    no separate modulation-matrix node needed. ADSR's <code>audio_in</code> and Filter's
    cutoff aren't wired up on the C++ side yet (see server script docstring, gaps #2, #5).
    "Synced Pair" bypasses the graph entirely and must be the only node besides Output
    (gap #3) — remove everything else before using it.
  </div>

  <div id="lg_output" style="margin-top: 16px;">
    <pre id="lg_summary"></pre>
    <img id="lg_plot">
    <audio id="lg_audio" controls></audio>
  </div>

<script>
function addModSocket(node, slotName, depthDefault, curveDefault) {
    node.addInput(slotName, "audio");
    node.properties[slotName + "_depth"] = depthDefault;
    node.properties[slotName + "_curve"] = curveDefault;
    node.addWidget("number", slotName + " depth", depthDefault,
        (v) => { node.properties[slotName + "_depth"] = v; }, { min: -1, max: 1, step: 0.05 });
    node.addWidget("combo", slotName + " curve", curveDefault,
        (v) => { node.properties[slotName + "_curve"] = v; },
        { values: ["Linear", "Exponential", "Logarithmic", "SCurve"] });
}

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
    addModSocket(this, "freq_mod", 0.0, "Linear");
    addModSocket(this, "amp_mod", 0.0, "Linear");
    addModSocket(this, "pulse_width_mod", 0.0, "Linear");
    this.size = [220, 320];
}
CaspyOscillator.title = "Oscillator";
LiteGraph.registerNodeType("caspy/oscillator", CaspyOscillator);

function CaspyWavetable() {
    this.addOutput("audio", "audio");
    this.properties = {
        frequency: 440, amplitude: 1.0, interpolation: "Linear",
        table_count: "1", table_0: "Sine", table_1: "Saw", table_2: "Triangle", table_3: "Sine",
        morph_position: 0.0,
    };
    this.addWidget("combo", "table_count", this.properties.table_count,
        (v) => { this.properties.table_count = v; },
        { values: ["1", "4"] });
    this.addWidget("combo", "table_0", this.properties.table_0,
        (v) => { this.properties.table_0 = v; }, { values: ["Sine", "Saw", "Triangle"] });
    this.addWidget("combo", "table_1", this.properties.table_1,
        (v) => { this.properties.table_1 = v; }, { values: ["Sine", "Saw", "Triangle"] });
    this.addWidget("combo", "table_2", this.properties.table_2,
        (v) => { this.properties.table_2 = v; }, { values: ["Sine", "Saw", "Triangle"] });
    this.addWidget("combo", "table_3", this.properties.table_3,
        (v) => { this.properties.table_3 = v; }, { values: ["Sine", "Saw", "Triangle"] });
    this.addWidget("number", "frequency", this.properties.frequency,
        (v) => { this.properties.frequency = v; }, { min: 20, max: 5000 });
    this.addWidget("number", "amplitude", this.properties.amplitude,
        (v) => { this.properties.amplitude = v; }, { min: 0, max: 1 });
    this.addWidget("number", "morph_position", this.properties.morph_position,
        (v) => { this.properties.morph_position = v; }, { min: 0, max: 1 });
    this.addWidget("combo", "interpolation", this.properties.interpolation,
        (v) => { this.properties.interpolation = v; }, { values: ["Linear", "Hermite"] });
    addModSocket(this, "freq_mod", 0.0, "Linear");
    addModSocket(this, "amp_mod", 0.0, "Linear");
    addModSocket(this, "morph_mod", 0.0, "Linear");
    this.size = [230, 420];
}
CaspyWavetable.title = "Wavetable (table_0 used when table_count=1)";
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
    addModSocket(this, "amp_mod", 0.0, "Linear");
    this.size = [200, 200];
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
    this.size = [220, 190];
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
CaspyFilter.title = "Filter (cutoff not modulatable yet — gap #5)";
LiteGraph.registerNodeType("caspy/filter", CaspyFilter);

function CaspySyncPair() {
    this.addOutput("audio", "audio");
    this.properties = {
        primary_waveform: "Sine", primary_frequency: 220,
        secondary_waveform: "Saw", secondary_frequency: 220,
    };
    this.addWidget("combo", "primary_waveform", this.properties.primary_waveform,
        (v) => { this.properties.primary_waveform = v; }, { values: ["Sine", "Saw", "Triangle"] });
    this.addWidget("number", "primary_frequency", this.properties.primary_frequency,
        (v) => { this.properties.primary_frequency = v; }, { min: 20, max: 5000 });
    this.addWidget("combo", "secondary_waveform", this.properties.secondary_waveform,
        (v) => { this.properties.secondary_waveform = v; }, { values: ["Sine", "Saw", "Triangle"] });
    this.addWidget("number", "secondary_frequency", this.properties.secondary_frequency,
        (v) => { this.properties.secondary_frequency = v; }, { min: 20, max: 5000 });
    this.size = [220, 160];
}
CaspySyncPair.title = "Synced Pair (must be sole node — gap #3)";
LiteGraph.registerNodeType("caspy/syncpair", CaspySyncPair);

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
    "caspy/syncpair": "SyncedOscillatorPair",
    "caspy/output": "Output",
};

const graph = new LGraph();
const canvas = new LGraphCanvas("#caspy_canvas", graph);
graph.start();

function addNode(type) {
    const node = LiteGraph.createNode(type);
    node.pos = [50 + Math.random() * 700, 50 + Math.random() * 380];
    graph.add(node);
}

document.getElementById("lg_add_osc").onclick = () => addNode("caspy/oscillator");
document.getElementById("lg_add_wavetable").onclick = () => addNode("caspy/wavetable");
document.getElementById("lg_add_noise").onclick = () => addNode("caspy/noise");
document.getElementById("lg_add_lfo").onclick = () => addNode("caspy/lfo");
document.getElementById("lg_add_adsr").onclick = () => addNode("caspy/adsr");
document.getElementById("lg_add_filter").onclick = () => addNode("caspy/filter");
document.getElementById("lg_add_syncpair").onclick = () => addNode("caspy/syncpair");
document.getElementById("lg_add_output").onclick = () => addNode("caspy/output");

function findNodeById(id) {
    return graph._nodes.find(n => n.id === id);
}

document.getElementById("lg_render").onclick = async () => {
    const nodes = graph._nodes.map(n => ({
        id: String(n.id),
        type: TYPE_MAP[n.type],
        name: n.title || TYPE_MAP[n.type],
        params: n.properties,
    }));
    const connections = Object.values(graph.links || {}).map(link => {
        const originNode = findNodeById(link.origin_id);
        const targetNode = findNodeById(link.target_id);
        const srcSlot = originNode && originNode.outputs ? originNode.outputs[link.origin_slot] : null;
        const dstSlot = targetNode && targetNode.inputs ? targetNode.inputs[link.target_slot] : null;
        return {
            source: String(link.origin_id),
            target: String(link.target_id),
            src_port: link.origin_slot,
            dst_port: link.target_slot,
            src_slot_name: srcSlot ? srcSlot.name : null,
            dst_slot_name: dstSlot ? dstSlot.name : null,
        };
    });
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