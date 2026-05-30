"""
test_caspi_bindings.py
======================
Smoke + behavioural tests for the CASPy Python bindings.

Run with:
    uv run --no-sync pytest Tests/ -v
    uv run --no-sync pytest Tests/ -v -k graph
"""

import math
import numpy as np
import pytest

try:
    import caspy
except ImportError as e:
    pytest.exit(f"caspy import failed — rebuild the extension.\nDetails: {e}", returncode=1)

wt = caspy.wavetable

SR       = 44100.0
FRAMES   = 512
CHANNELS = 1


# ===========================================================================
# Helpers — always construct nodes fresh; never touch the Python object
#           after passing it to add_node().
# ===========================================================================

def make_sine_bank() -> wt.WaveTableBank1:
    bank = wt.WaveTableBank1()
    bank[0].fill_sine()
    return bank


def make_morph_bank() -> wt.WaveTableBank4:
    bank = wt.WaveTableBank4()
    bank[0].fill_sine()
    bank[1].fill_saw()
    bank[2].fill_triangle()
    bank[3].fill_with(lambda t: math.sin(4 * math.pi * t))
    return bank


def add_osc1(g, bank, hz=440.0):
    """Add a WavetableOscillator1 to graph g; return its NodeId."""
    return g.add_node(wt.WavetableOscillator1(bank, SR, hz))


def add_osc4(g, bank, hz=440.0):
    """Add a WavetableOscillator4 to graph g; return its NodeId."""
    return g.add_node(wt.WavetableOscillator4(bank, SR, hz))


def add_filter(g, cutoff=1000.0, mode=None):
    """Add an SvfFilter to graph g; return its NodeId."""
    if mode is None:
        mode = caspy.FilterMode.LowPass
    return g.add_node(caspy.SvfFilter(SR, cutoff, 0.707, mode))


# ===========================================================================
# Module import
# ===========================================================================

class TestImport:
    def test_module_loadable(self):
        assert caspy is not None

    def test_wavetable_submodule(self):
        assert wt is not None

    def test_enums_present(self):
        _ = caspy.FilterMode.LowPass
        _ = caspy.oscillators.WaveShape.Sine
        _ = caspy.NodeType.Audio
        _ = caspy.ConnectionType.Audio
        _ = caspy.GraphError.CycleDetected
        _ = wt.InterpolationMode.Linear

    def test_classes_present(self):
        for name in ("AudioGraph", "SvfFilter", "NodeBase"):
            assert hasattr(caspy, name), f"caspy.{name} missing"
        for name in ("WaveTable", "WaveTableBank1", "WaveTableBank4",
                     "WavetableOscillator1", "WavetableOscillator4"):
            assert hasattr(wt, name), f"caspy.wavetable.{name} missing"


# ===========================================================================
# WaveTable
# ===========================================================================

class TestWaveTable:
    def test_default_construct(self):
        t = wt.WaveTable()
        assert len(t) == 2048

    def test_fill_sine_range(self):
        t = wt.WaveTable()
        t.fill_sine()
        arr = t.to_numpy()
        assert arr.shape == (2048,)
        assert float(np.max(np.abs(arr))) == pytest.approx(1.0, abs=1e-4)

    def test_fill_saw_range(self):
        t = wt.WaveTable()
        t.fill_saw()
        arr = t.to_numpy()
        assert float(np.min(arr)) >= -1.0 - 1e-6
        assert float(np.max(arr)) <= 1.0 + 1e-6

    def test_fill_triangle_range(self):
        t = wt.WaveTable()
        t.fill_triangle()
        arr = t.to_numpy()
        assert float(np.min(arr)) >= -1.0 - 1e-6
        assert float(np.max(arr)) <= 1.0 + 1e-6

    def test_fill_with_callable(self):
        t = wt.WaveTable()
        t.fill_with(lambda ph: math.sin(2 * math.pi * ph))
        assert t.read_linear(0.25) == pytest.approx(1.0, abs=0.01)

    def test_fill_chaining(self):
        t = wt.WaveTable()
        result = t.fill_sine()
        assert result is t

    def test_to_numpy_dtype(self):
        t = wt.WaveTable()
        t.fill_sine()
        assert t.to_numpy().dtype == np.float32

    def test_read_linear_phase_zero(self):
        t = wt.WaveTable()
        t.fill_sine()
        assert t.read_linear(0.0) == pytest.approx(0.0, abs=1e-4)

    def test_read_hermite_close_to_linear_sine(self):
        t = wt.WaveTable()
        t.fill_sine()
        assert t.read_linear(0.125) == pytest.approx(t.read_hermite(0.125), abs=0.01)

    def test_setitem_getitem(self):
        t = wt.WaveTable()
        t[0] = 0.5
        assert t[0] == pytest.approx(0.5, abs=1e-6)


# ===========================================================================
# WaveTableBank
# ===========================================================================

class TestWaveTableBank:
    def test_bank1_num_tables(self):
        assert wt.WaveTableBank1.num_tables == 1

    def test_bank4_num_tables(self):
        assert wt.WaveTableBank4.num_tables == 4

    def test_bank_table_size(self):
        assert wt.WaveTableBank1.table_size == 2048

    def test_bank1_index_access(self):
        bank = wt.WaveTableBank1()
        bank[0].fill_sine()
        assert len(bank[0]) == 2048

    def test_bank4_all_tables_accessible(self):
        bank = make_morph_bank()
        for i in range(4):
            assert len(bank[i]) == 2048

    def test_fill_all(self):
        bank = wt.WaveTableBank4()
        bank.fill_all(lambda t: 0.5)
        for i in range(4):
            assert bank[i][0] == pytest.approx(0.5, abs=1e-6)


# ===========================================================================
# WavetableOscillator1  (standalone — no graph)
# ===========================================================================

class TestWavetableOsc1:
    def setup_method(self):
        self.bank = make_sine_bank()
        self.osc  = wt.WavetableOscillator1(self.bank, SR, 440.0)

    def test_render_returns_ndarray(self):
        audio = self.osc.render(FRAMES)
        assert isinstance(audio, np.ndarray)
        assert audio.dtype == np.float32
        assert len(audio) == FRAMES

    def test_render_not_silent(self):
        assert float(np.max(np.abs(self.osc.render(FRAMES)))) > 0.1

    def test_render_bounded(self):
        assert float(np.max(np.abs(self.osc.render(FRAMES * 4)))) <= 1.1

    def test_render_sample_returns_float(self):
        assert isinstance(self.osc.render_sample(), float)

    def test_frequency_changes_pitch(self):
        bank = make_sine_bank()
        a = wt.WavetableOscillator1(bank, SR, 220.0).render(FRAMES)
        b = wt.WavetableOscillator1(bank, SR, 880.0).render(FRAMES)
        assert not np.allclose(a, b)

    def test_amplitude_scaling(self):
        bank = make_sine_bank()
        full = wt.WavetableOscillator1(bank, SR, 440.0)
        half = wt.WavetableOscillator1(bank, SR, 440.0)
        half.set_amplitude(0.5)
        ratio = (float(np.max(np.abs(full.render(FRAMES)))) /
                 float(np.max(np.abs(half.render(FRAMES)))))
        assert ratio == pytest.approx(2.0, rel=0.1)

    def test_reset_phase_reproducible(self):
        a = self.osc.render(FRAMES)
        self.osc.reset_phase()
        b = self.osc.render(FRAMES)
        assert np.allclose(a, b, atol=1e-4)

    def test_phase_wrapped_flag(self):
        wrapped_seen = False
        for _ in range(int(SR / 440) + 10):
            self.osc.render_sample()
            if self.osc.phase_wrapped():
                wrapped_seen = True
                break
        assert wrapped_seen

    def test_interpolation_mode_hermite(self):
        self.osc.set_interpolation_mode(wt.InterpolationMode.Hermite)
        assert len(self.osc.render(FRAMES)) == FRAMES

    def test_set_phase_offset(self):
        self.osc.set_phase_offset(0.25)
        self.osc.reset_phase()
        assert isinstance(self.osc.render_sample(), float)

    def test_modulatable_parameters_accessible(self):
        _ = self.osc.frequency
        _ = self.osc.amplitude
        _ = self.osc.morph_position


# ===========================================================================
# WavetableOscillator4  (standalone — no graph)
# ===========================================================================

class TestWavetableOsc4:
    def setup_method(self):
        self.bank = make_morph_bank()
        self.osc  = wt.WavetableOscillator4(self.bank, SR, 440.0)

    def test_render_not_silent(self):
        assert float(np.max(np.abs(self.osc.render(FRAMES)))) > 0.1

    def test_morph_changes_waveform(self):
        bank = make_morph_bank()
        a = wt.WavetableOscillator4(bank, SR, 440.0)
        b = wt.WavetableOscillator4(bank, SR, 440.0)
        a.set_morph_position(0.0)
        b.set_morph_position(3.0)
        assert not np.allclose(a.render(FRAMES), b.render(FRAMES), atol=0.01)

    def test_hard_sync_1_4_returns_tuple(self):
        primary = wt.WavetableOscillator1(make_sine_bank(), SR, 220.0)
        audio, sync_pts = wt.render_hard_sync(primary, self.osc, FRAMES)
        assert isinstance(audio, np.ndarray)
        assert len(audio) == FRAMES
        assert isinstance(sync_pts, list)
        assert len(sync_pts) > 0

    def test_hard_sync_4_4(self):
        bank = make_morph_bank()
        primary   = wt.WavetableOscillator4(bank, SR, 220.0)
        secondary = wt.WavetableOscillator4(bank, SR, 660.0)
        audio, _ = wt.render_hard_sync(primary, secondary, FRAMES)
        assert len(audio) == FRAMES


# ===========================================================================
# SvfFilter  (standalone — no graph)
# ===========================================================================

class TestSvfFilter:
    def test_construct_default(self):
        assert caspy.SvfFilter() is not None

    def test_construct_full(self):
        assert caspy.SvfFilter(SR, 1000.0, 0.707, caspy.FilterMode.LowPass) is not None

    def test_process_sample_returns_float(self):
        assert isinstance(caspy.SvfFilter(SR, 1000.0).process_sample(1.0), float)

    def test_process_block_shape(self):
        f   = caspy.SvfFilter(SR, 1000.0)
        out = f.process_block(np.random.randn(FRAMES).astype(np.float32))
        assert out.shape == (FRAMES,)
        assert out.dtype == np.float32

    def test_lowpass_attenuates_high_freq(self):
        f = caspy.SvfFilter(SR, 500.0, 0.707, caspy.FilterMode.LowPass)
        assert f.frequency_response(100.0) > f.frequency_response(5000.0) * 10

    def test_highpass_attenuates_low_freq(self):
        f = caspy.SvfFilter(SR, 5000.0, 0.707, caspy.FilterMode.HighPass)
        assert f.frequency_response(SR / 2 * 0.9) > f.frequency_response(200.0) * 10

    def test_mode_property_roundtrip(self):
        f = caspy.SvfFilter(SR, 1000.0)
        f.mode = caspy.FilterMode.BandPass
        assert f.mode == caspy.FilterMode.BandPass

    def test_cutoff_property_roundtrip(self):
        f = caspy.SvfFilter(SR, 1000.0)
        f.cutoff = 2000.0
        assert f.cutoff == pytest.approx(2000.0, rel=1e-4)

    def test_q_property_roundtrip(self):
        f = caspy.SvfFilter(SR, 1000.0)
        f.q = 2.0
        assert f.q == pytest.approx(2.0, rel=1e-4)

    def test_reset_clears_state(self):
        f = caspy.SvfFilter(SR, 1000.0)
        for _ in range(100):
            f.process_sample(1.0)
        f.reset()
        assert abs(f.process_sample(0.0)) < 1e-6

    def test_all_modes_produce_output(self):
        noise = np.random.randn(256).astype(np.float32)
        for mode in (caspy.FilterMode.LowPass, caspy.FilterMode.HighPass,
                     caspy.FilterMode.BandPass, caspy.FilterMode.Notch,
                     caspy.FilterMode.Peak, caspy.FilterMode.AllPass):
            f   = caspy.SvfFilter(SR, 1000.0, 0.707, mode)
            out = f.process_block(noise)
            assert float(np.max(np.abs(out))) > 0.0, f"Mode {mode} produced silence"


# ===========================================================================
# AudioGraph — basic node management
# ===========================================================================

class TestAudioGraphBasic:
    def test_construct_empty(self):
        g = caspy.AudioGraph()
        assert g.get_num_nodes() == 0
        assert g.get_num_connections() == 0

    def test_not_prepared_initially(self):
        assert not caspy.AudioGraph().is_prepared()

    def test_add_node_returns_int_id(self):
        g    = caspy.AudioGraph()
        nid  = add_osc1(g, make_sine_bank())
        assert isinstance(nid, int)
        assert g.get_num_nodes() == 1

    def test_multiple_node_ids_distinct(self):
        g    = caspy.AudioGraph()
        bank = make_sine_bank()
        ids  = [add_osc1(g, bank) for _ in range(3)]
        assert len(set(ids)) == 3
        assert g.get_num_nodes() == 3

    def test_get_node_by_id(self):
        g   = caspy.AudioGraph()
        nid = add_osc1(g, make_sine_bank())
        node = g.get_node(nid)
        assert node is not None
        assert node.get_id() == nid

    def test_get_node_invalid_raises(self):
        with pytest.raises(ValueError):
            caspy.AudioGraph().get_node(9999)

    def test_remove_node(self):
        g   = caspy.AudioGraph()
        nid = add_osc1(g, make_sine_bank())
        g.remove_node(nid)
        assert g.get_num_nodes() == 0

    def test_remove_invalid_node_raises(self):
        with pytest.raises(ValueError):
            caspy.AudioGraph().remove_node(9999)

    def test_prepare_sets_prepared(self):
        g   = caspy.AudioGraph()
        add_osc1(g, make_sine_bank())
        g.prepare(CHANNELS, FRAMES, SR)
        assert g.is_prepared()

    def test_add_node_after_prepare_clears_prepared(self):
        g    = caspy.AudioGraph()
        bank = make_sine_bank()
        add_osc1(g, bank)
        g.prepare(CHANNELS, FRAMES, SR)
        add_osc1(g, bank)           # second add invalidates prepare
        assert not g.is_prepared()

    def test_topological_order_nonempty_after_prepare(self):
        g   = caspy.AudioGraph()
        add_osc1(g, make_sine_bank())
        g.prepare(CHANNELS, FRAMES, SR)
        assert len(g.get_sorted_order()) == 1


# ===========================================================================
# AudioGraph — connections
# ===========================================================================

class TestAudioGraphConnections:
    def _osc_filter_graph(self):
        """Prepared graph with osc -> filter. Returns (g, osc_id, filt_id)."""
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        g.connect(osc_id, filt_id)
        g.prepare(CHANNELS, FRAMES, SR)
        return g, osc_id, filt_id

    def test_connect_port0_to_port0(self):
        g, _, _ = self._osc_filter_graph()
        assert g.get_num_connections() == 1

    def test_connect_explicit_ports(self):
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        g.connect(osc_id, 0, filt_id, 0)
        assert g.get_num_connections() == 1

    def test_duplicate_connection_raises(self):
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        g.connect(osc_id, filt_id)
        with pytest.raises(ValueError, match="duplicate"):
            g.connect(osc_id, filt_id)

    def test_connect_invalid_node_raises(self):
        g      = caspy.AudioGraph()
        osc_id = add_osc1(g, make_sine_bank())
        with pytest.raises(ValueError):
            g.connect(osc_id, 9999)

    def test_disconnect(self):
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        g.connect(osc_id, filt_id)
        g.disconnect(osc_id, 0, filt_id, 0)
        assert g.get_num_connections() == 0

    def test_disconnect_nonexistent_raises(self):
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        with pytest.raises(ValueError):
            g.disconnect(osc_id, 0, filt_id, 0)

    def test_remove_node_clears_its_connections(self):
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        g.connect(osc_id, filt_id)
        g.remove_node(osc_id)
        assert g.get_num_connections() == 0

    def test_cycle_detection_raises(self):
        # Cycle needs nodes with both inputs and outputs — use two filters.
        # osc -> filt_a -> filt_b -> filt_a (cycle on the non-feedback edge)
        g      = caspy.AudioGraph()
        osc_id = add_osc1(g, make_sine_bank())
        fa_id  = add_filter(g, cutoff=800.0)
        fb_id  = add_filter(g, cutoff=1200.0)
        g.connect(osc_id, fa_id)
        g.connect(fa_id, fb_id)
        g.connect(fb_id, fa_id)   # back-edge without feedback flag → cycle
        with pytest.raises(ValueError, match="cycle"):
            g.prepare(CHANNELS, FRAMES, SR)

    def test_feedback_connection_bypasses_cycle_check(self):
        # Same topology but the back-edge is marked as feedback — must succeed.
        g      = caspy.AudioGraph()
        osc_id = add_osc1(g, make_sine_bank())
        fa_id  = add_filter(g, cutoff=800.0)
        fb_id  = add_filter(g, cutoff=1200.0)
        g.connect(osc_id, fa_id)
        g.connect(fa_id, fb_id)
        g.connect_feedback(fb_id, fa_id)   # back-edge as feedback — no cycle
        g.prepare(CHANNELS, FRAMES, SR)    # must not raise
        assert g.is_prepared()


# ===========================================================================
# AudioGraph — rendering
# ===========================================================================

class TestAudioGraphRender:
    def test_single_osc_render_shape(self):
        g      = caspy.AudioGraph()
        osc_id = add_osc1(g, make_sine_bank())
        audio  = g.render(osc_id, num_blocks=4, channels=CHANNELS, frames=FRAMES, sample_rate=SR)
        assert audio.shape == (CHANNELS, 4 * FRAMES)
        assert audio.dtype == np.float32

    def test_render_not_silent(self):
        g      = caspy.AudioGraph()
        osc_id = add_osc1(g, make_sine_bank())
        audio  = g.render(osc_id, num_blocks=2, channels=CHANNELS, frames=FRAMES, sample_rate=SR)
        assert float(np.max(np.abs(audio))) > 0.1

    def test_process_then_get_buffer(self):
        g      = caspy.AudioGraph()
        osc_id = add_osc1(g, make_sine_bank())
        g.prepare(CHANNELS, FRAMES, SR)
        g.process()
        buf = g.get_node(osc_id).get_output_buffer()
        assert buf.shape == (CHANNELS, FRAMES)

    def test_osc_filter_chain_render(self):
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        g.connect(osc_id, filt_id)
        audio = g.render(filt_id, num_blocks=4, channels=CHANNELS, frames=FRAMES, sample_rate=SR)
        assert audio.shape == (CHANNELS, 4 * FRAMES)
        assert float(np.max(np.abs(audio))) > 0.0

    def test_lowpass_filter_attenuates_in_graph(self):
        """440 Hz sine through 200 Hz LP should be quieter than unfiltered."""
        bank = make_sine_bank()

        g_raw  = caspy.AudioGraph()
        raw_id = add_osc1(g_raw, bank)
        raw    = g_raw.render(raw_id, num_blocks=8, channels=CHANNELS,
                              frames=FRAMES, sample_rate=SR)

        g_filt  = caspy.AudioGraph()
        osc_id  = add_osc1(g_filt, bank)
        filt_id = add_filter(g_filt, cutoff=200.0)
        g_filt.connect(osc_id, filt_id)
        filtered = g_filt.render(filt_id, num_blocks=8, channels=CHANNELS,
                                 frames=FRAMES, sample_rate=SR)

        rms = lambda x: float(np.sqrt(np.mean(x**2)))
        assert rms(filtered) < rms(raw) * 0.5   # > 6 dB attenuation

    def test_sorted_order_osc_before_filter(self):
        g       = caspy.AudioGraph()
        osc_id  = add_osc1(g, make_sine_bank())
        filt_id = add_filter(g)
        g.connect(osc_id, filt_id)
        g.prepare(CHANNELS, FRAMES, SR)
        order = list(g.get_sorted_order())
        assert order.index(osc_id) < order.index(filt_id)

    def test_morph_osc_in_graph(self):
        g      = caspy.AudioGraph()
        osc_id = add_osc4(g, make_morph_bank(), hz=220.0)
        g.get_node(osc_id).set_morph_position(1.5)
        audio  = g.render(osc_id, num_blocks=2, channels=CHANNELS,
                          frames=FRAMES, sample_rate=SR)
        assert float(np.max(np.abs(audio))) > 0.0

    def test_get_output_buffer_explicit_port(self):
        g      = caspy.AudioGraph()
        osc_id = add_osc1(g, make_sine_bank())
        g.prepare(CHANNELS, FRAMES, SR)
        g.process()
        buf = g.get_node(osc_id).get_output_buffer(0)
        assert buf.shape == (CHANNELS, FRAMES)