/*
 * bind_graph.cpp
 *
 * Bindings for AudioGraph<float>, NodeBase<float>, Port, Connection,
 * and all graph-related enumerations.
 *
 * OWNERSHIP MODEL
 *
 * AudioGraph::addNode() takes unique_ptr<NodeBase<float>> ownership.
 * Python constructs nodes with py::nodelete so pybind11 never calls
 * delete on them. add_node() recovers the raw pointer, wraps it in a
 * unique_ptr, and transfers it to the graph. After add_node() the
 * Python node object must not be used — only the returned NodeId matters.
 *
 * All concrete node classes bound in bind_oscillators.cpp,
 * bind_filters.cpp etc. must:
 *   - Use py::nodelete as their holder type.
 *   - Declare NodeBase<float> as their pybind11 base.
 *   - Use py::init([]{ return new T(...); }) for heap allocation.
 *
 * CONNECT API
 *
 * The Python connect() mirrors the C++ Port-based overloads:
 *
 *   g.connect(osc_id, filt_id)           # port 0 -> port 0
 *   g.connect(osc_id, 0, filt_id, 0)     # explicit ports
 *   g.connect_control(lfo_id, filt_id, 1)  # control, dst port 1
 *   g.connect_feedback(delay_out, delay_in) # feedback back-edge
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "caspi.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"
#include "filters/caspi_Filter.h"
#include "oscillators/caspi_BlepOscillator.h"
#include "synthesizers/caspi_Voice.h"

namespace py = pybind11;

using namespace CASPI;
using namespace CASPI::Graph;

using F          = float;
using Graph_t    = AudioGraph<F>;
using NodeBase_t = NodeBase<F>;

static py::array_t<F> node_output_to_numpy (const NodeBase_t& node,
                                             std::size_t portIndex = 0)
{
    const auto* buf = node.getOutputBuffer (portIndex);
    if (buf == nullptr)
    {
        return py::array_t<F> (
            { static_cast<py::ssize_t> (0), static_cast<py::ssize_t> (0) });
    }

    const std::size_t C = buf->numChannels();
    const std::size_t N = buf->numFrames();

    py::array_t<F> out ({
        static_cast<py::ssize_t> (C),
        static_cast<py::ssize_t> (N)
    });
    auto r = out.mutable_unchecked<2>();

    for (std::size_t ch = 0; ch < C; ++ch)
    {
        for (std::size_t fr = 0; fr < N; ++fr)
        {
            r (static_cast<py::ssize_t> (ch),
               static_cast<py::ssize_t> (fr)) = buf->sample (ch, fr);
        }
    }
    return out;
}

void bind_graph (py::module_& m)
{
    /*
     * Enumerations — registered once here, nowhere else.
     */
    py::enum_<NodeType> (m, "NodeType")
        .value ("Audio",   NodeType::Audio)
        .value ("Control", NodeType::Control)
        .export_values();

    py::enum_<ConnectionType> (m, "ConnectionType")
        .value ("Audio",   ConnectionType::Audio)
        .value ("Control", ConnectionType::Control)
        .export_values();

    py::enum_<GraphError> (m, "GraphError")
        .value ("InvalidNodeId",       GraphError::InvalidNodeId)
        .value ("InvalidPort",         GraphError::InvalidPort)
        .value ("DuplicateConnection", GraphError::DuplicateConnection)
        .value ("ConnectionNotFound",  GraphError::ConnectionNotFound)
        .value ("NodeNotFound",        GraphError::NodeNotFound)
        .value ("CycleDetected",       GraphError::CycleDetected)
        .value ("TypeMismatch",        GraphError::TypeMismatch)
        .value ("NotPrepared",         GraphError::NotPrepared)
        .value ("NullNode",            GraphError::NullNode)
        .export_values();

    /*
     * Connection (read-only struct)
     */
    py::class_<Connection> (m, "Connection")
        .def_readonly ("source_node",      &Connection::sourceNode)
        .def_readonly ("source_port",      &Connection::sourcePort)
        .def_readonly ("destination_node", &Connection::destinationNode)
        .def_readonly ("destination_port", &Connection::destinationPort)
        .def_readonly ("connection_type",  &Connection::connectionType)
        .def_readonly ("is_feedback",      &Connection::isFeedback)
        .def ("__repr__", [] (const Connection& c)
        {
            return "<Connection "
                + std::to_string (c.sourceNode)      + ":"
                + std::to_string (c.sourcePort)      + " -> "
                + std::to_string (c.destinationNode) + ":"
                + std::to_string (c.destinationPort)
                + (c.isFeedback ? " [feedback]" : "") + ">";
        });

    /*
     * NodeBase<float>
     *
     * Registered with py::nodelete — pybind11 never calls delete.
     * All concrete node classes use this as their pybind11 base.
     */
    py::class_<NodeBase_t, std::unique_ptr<NodeBase_t, py::nodelete>> (m, "NodeBase")
        .def ("get_id",               &NodeBase_t::getId)
        .def ("get_type",             &NodeBase_t::getType)
        .def ("get_num_input_ports",  &NodeBase_t::getNumInputPorts)
        .def ("get_num_output_ports", &NodeBase_t::getNumOutputPorts)
        .def ("is_prepared",          &NodeBase_t::isPrepared)
        .def ("get_sample_rate",      &NodeBase_t::getSampleRate)
        .def ("get_output_buffer",
            [] (const NodeBase_t& self, std::size_t port)
            { return node_output_to_numpy (self, port); },
            py::arg ("port") = 0u,
            "Return output buffer as numpy array [channels, frames]. Valid after process().");

    /*
     * AudioGraph<float>
     */
    py::class_<Graph_t> (m, "AudioGraph",
        R"pbdoc(
        Directed acyclic graph of audio processing nodes.

        Nodes are constructed and immediately transferred:
            osc_id = g.add_node(caspy.BlepOscillator())
            g.get_node(osc_id).set_frequency(440.0)
            g.prepare(num_channels=1, num_frames=512, sample_rate=44100.0)
            g.process()
            audio = g.get_node(osc_id).get_output_buffer()

        Connection shortcuts:
            g.connect(osc_id, filt_id)              # port 0 to port 0
            g.connect(osc_id, 0, filt_id, 0)        # explicit
            g.connect_control(lfo_id, 0, filt_id, 1)
            g.connect_feedback(src_id, dst_id)

        Render helper:
            audio = g.render(output_id, num_blocks=86, channels=1,
                             frames=512, sample_rate=44100.0)
        )pbdoc")
        .def (py::init<>())

        /*
         * add_node
         *
         * Recovers the raw pointer from the py::nodelete holder via cast,
         * wraps it in a unique_ptr, and transfers ownership to the graph.
         * Sets _consumed on the Python object to prevent use-after-transfer.
         */
        .def ("add_node",
            [] (Graph_t& self, py::object nodeObj) -> NodeId
            {
                NodeBase_t* raw = nodeObj.cast<NodeBase_t*>();
                if (raw == nullptr)
                {
                    throw py::value_error ("add_node: node is None");
                }

                auto result = self.addNode (std::unique_ptr<NodeBase_t> (raw));
                if (! result.has_value())
                {
                    throw py::value_error ("add_node: graph rejected the node");
                }

                nodeObj.attr ("_consumed") = true;
                return result.value();
            },
            py::arg ("node"),
            "Transfer node ownership to the graph. Returns NodeId. Do not use the node object afterwards.")

        .def ("remove_node",
            [] (Graph_t& self, NodeId id)
            {
                auto r = self.removeNode (id);
                if (! r.has_value())
                {
                    throw py::value_error ("remove_node: node not found");
                }
            },
            py::arg ("id"))

        /*
         * connect — port 0 to port 0 shorthand
         */
        .def ("connect",
            [] (Graph_t& self, NodeId src, NodeId dst)
            {
                auto r = self.connect (src, dst);
                if (! r.has_value())
                {
                    throw py::value_error (
                        std::string ("connect: ") + [&]
                        {
                            switch (r.error())
                            {
                                case GraphError::InvalidNodeId:       return "invalid node id";
                                case GraphError::InvalidPort:         return "port out of range";
                                case GraphError::DuplicateConnection: return "duplicate connection";
                                case GraphError::TypeMismatch:        return "type mismatch";
                                default:                              return "unknown error";
                            }
                        }());
                }
            },
            py::arg ("src_node"),
            py::arg ("dst_node"),
            "Connect src port 0 to dst port 0 (audio). Shorthand for the common case.")

        /*
         * connect — explicit ports
         */
        .def ("connect",
            [] (Graph_t& self,
                NodeId src, std::size_t srcPort,
                NodeId dst, std::size_t dstPort)
            {
                auto r = self.connect (Port (src, srcPort), Port (dst, dstPort));
                if (! r.has_value())
                {
                    throw py::value_error (
                        std::string ("connect: ") + [&]
                        {
                            switch (r.error())
                            {
                                case GraphError::InvalidNodeId:       return "invalid node id";
                                case GraphError::InvalidPort:         return "port out of range";
                                case GraphError::DuplicateConnection: return "duplicate connection";
                                case GraphError::TypeMismatch:        return "type mismatch";
                                default:                              return "unknown error";
                            }
                        }());
                }
            },
            py::arg ("src_node"),
            py::arg ("src_port"),
            py::arg ("dst_node"),
            py::arg ("dst_port"),
            "Connect explicit audio ports.")

        /*
         * connect_control
         */
        .def ("connect_control",
            [] (Graph_t& self,
                NodeId src, std::size_t srcPort,
                NodeId dst, std::size_t dstPort)
            {
                auto r = self.connectControl (Port (src, srcPort), Port (dst, dstPort));
                if (! r.has_value())
                {
                    throw py::value_error ("connect_control: " + std::to_string (static_cast<int> (r.error())));
                }
            },
            py::arg ("src_node"),
            py::arg ("src_port") = 0u,
            py::arg ("dst_node") = INVALID_NODE_ID,
            py::arg ("dst_port") = 0u,
            "Connect a ControlNode output to a destination input port.")

        /*
         * connect_feedback
         */
        .def ("connect_feedback",
            [] (Graph_t& self, NodeId src, NodeId dst)
            {
                auto r = self.connectFeedback (Port (src), Port (dst));
                if (! r.has_value())
                {
                    throw py::value_error ("connect_feedback: " + std::to_string (static_cast<int> (r.error())));
                }
            },
            py::arg ("src_node"),
            py::arg ("dst_node"),
            "Connect a feedback back-edge (port 0 to port 0). Reads previous-block output.")

        .def ("disconnect",
            [] (Graph_t& self,
                NodeId src, std::size_t srcPort,
                NodeId dst, std::size_t dstPort,
                ConnectionType type,
                bool isFeedback)
            {
                auto r = self.disconnect (src, srcPort, dst, dstPort, type, isFeedback);
                if (! r.has_value())
                {
                    throw py::value_error ("disconnect: connection not found");
                }
            },
            py::arg ("src_node"),
            py::arg ("src_port")        = 0u,
            py::arg ("dst_node")        = INVALID_NODE_ID,
            py::arg ("dst_port")        = 0u,
            py::arg ("connection_type") = ConnectionType::Audio,
            py::arg ("is_feedback")     = false)

        .def ("prepare",
            [] (Graph_t& self, std::size_t channels, std::size_t frames, double sr)
            {
                auto r = self.prepare (channels, frames, sr);
                if (! r.has_value())
                {
                    if (r.error() == GraphError::CycleDetected)
                    {
                        throw py::value_error (
                            "prepare: cycle detected — use connect_feedback() for back-edges");
                    }
                    throw py::value_error ("prepare: failed");
                }
            },
            py::arg ("num_channels"),
            py::arg ("num_frames"),
            py::arg ("sample_rate"))

        .def ("process",             &Graph_t::process)
        .def ("is_prepared",         &Graph_t::isPrepared)
        .def ("get_num_nodes",       &Graph_t::getNumNodes)
        .def ("get_num_connections", &Graph_t::getNumConnections)
        .def ("get_sorted_order",    &Graph_t::getSortedOrder)

        /*
         * get_node
         *
         * Returns a reference_internal to the node. pybind11 dispatches to the
         * correct concrete Python type (e.g. BlepOscillator with set_frequency)
         * via the registered type hierarchy.
         */
        .def ("get_node",
            [] (Graph_t& self, NodeId id) -> NodeBase_t&
            {
                NodeBase_t* p = self.getNode (id);
                if (p == nullptr)
                {
                    throw py::value_error ("get_node: node not found");
                }
                return *p;
            },
            py::arg ("id"),
            py::return_value_policy::reference_internal,
            "Return a reference to the node. Valid while the node is in the graph.")

        /*
         * render — convenience: prepare if needed, run N blocks, return numpy array.
         */
        .def ("render",
            [] (Graph_t& self,
                NodeId      outputNodeId,
                std::size_t numBlocks,
                std::size_t channels,
                std::size_t frames,
                double      sr) -> py::array_t<F>
            {
                if (! self.isPrepared())
                {
                    auto r = self.prepare (channels, frames, sr);
                    if (! r.has_value())
                    {
                        throw py::value_error ("render: prepare failed");
                    }
                }

                const std::size_t total = numBlocks * frames;
                py::array_t<F> out ({
                    static_cast<py::ssize_t> (channels),
                    static_cast<py::ssize_t> (total)
                });
                auto buf = out.mutable_unchecked<2>();

                for (std::size_t block = 0; block < numBlocks; ++block)
                {
                    self.process();
                    const NodeBase_t* node = self.getNode (outputNodeId);
                    if (node == nullptr)
                    {
                        continue;
                    }
                    const auto* nodeBuf = node->getOutputBuffer (0);
                    if (nodeBuf == nullptr)
                    {
                        continue;
                    }
                    const std::size_t offset = block * frames;
                    for (std::size_t ch = 0; ch < channels; ++ch)
                    {
                        for (std::size_t fr = 0; fr < frames; ++fr)
                        {
                            buf (static_cast<py::ssize_t> (ch),
                                 static_cast<py::ssize_t> (offset + fr))
                                = nodeBuf->sample (ch, fr);
                        }
                    }
                }
                return out;
            },
            py::arg ("output_node_id"),
            py::arg ("num_blocks"),
            py::arg ("channels")    = 1u,
            py::arg ("frames")      = 512u,
            py::arg ("sample_rate") = 44100.0,
            "Render num_blocks blocks. Returns numpy [channels, num_blocks*frames].");
}