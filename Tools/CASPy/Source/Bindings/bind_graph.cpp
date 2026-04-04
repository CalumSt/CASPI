/*************************************************************************
 * @file bind_graph.cpp
 * @brief pybind11 bindings for AudioGraph<float>, AudioContext<float>,
 *        NodeBase<float>, and the connection/error types.
 *
 * Bound types (all float precision):
 *   caspi.NodeType            enum
 *   caspi.ConnectionType      enum
 *   caspi.GraphError          enum
 *   caspi.Connection          struct (read-only)
 *   caspi.AudioGraph          class
 *
 * Usage from Python:
 *   import caspi
 *   g = caspi.AudioGraph()
 *   # Add nodes via subclasses defined in other bind_*.cpp files
 *   # that expose Python-constructible nodes returning unique_ptr<NodeBase<float>>.
 ************************************************************************/

#include "caspi.h"
#include "core/caspi_Graph.h"
#include "core/caspi_Node.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace CASPI;
using namespace CASPI::Graph;

// Convenience aliases
using F          = float;
using Graph_t    = AudioGraph<F>;
using NodeBase_t = NodeBase<F>;
using Conn_t     = Connection;

void bind_graph (py::module_& m)
{
    // py::nodelete: pybind11 will not delete the raw pointer when the Python
    // object is GC'd. Ownership transfers to the graph on add_node().
    py::class_<NodeBase_t, std::shared_ptr<NodeBase_t>>(m, "NodeBase")
        .def ("get_id", &NodeBase_t::getId)
        .def ("get_type", &NodeBase_t::getType)
        .def ("get_num_input_ports", &NodeBase_t::getNumInputPorts)
        .def ("get_num_output_ports", &NodeBase_t::getNumOutputPorts)
        .def ("is_prepared", &NodeBase_t::isPrepared)
        .def ("get_sample_rate", &NodeBase_t::getSampleRate);

    py::enum_<NodeType> (m, "NodeType")
        .value ("Audio", NodeType::Audio)
        .value ("Control", NodeType::Control)
        .export_values();

    py::enum_<ConnectionType> (m, "ConnectionType")
        .value ("Audio", ConnectionType::Audio)
        .value ("Control", ConnectionType::Control)
        .export_values();

    py::enum_<GraphError> (m, "GraphError")
        .value ("InvalidNodeId", GraphError::InvalidNodeId)
        .value ("InvalidPort", GraphError::InvalidPort)
        .value ("DuplicateConnection", GraphError::DuplicateConnection)
        .value ("ConnectionNotFound", GraphError::ConnectionNotFound)
        .value ("NodeNotFound", GraphError::NodeNotFound)
        .value ("CycleDetected", GraphError::CycleDetected)
        .value ("TypeMismatch", GraphError::TypeMismatch)
        .value ("NotPrepared", GraphError::NotPrepared)
        .value ("NullNode", GraphError::NullNode)
        .export_values();

    py::class_<Conn_t> (m, "Connection")
        .def_readonly ("source_node", &Conn_t::sourceNode)
        .def_readonly ("source_port", &Conn_t::sourcePort)
        .def_readonly ("destination_node", &Conn_t::destinationNode)
        .def_readonly ("destination_port", &Conn_t::destinationPort)
        .def_readonly ("connection_type", &Conn_t::connectionType)
        .def_readonly ("is_feedback", &Conn_t::isFeedback)
        .def ("__repr__",
              [] (const Conn_t& c)
              {
                  return "<Connection src=" + std::to_string (c.sourceNode) + ":" + std::to_string (c.sourcePort)
                         + " -> dst=" + std::to_string (c.destinationNode) + ":" + std::to_string (c.destinationPort)
                         + (c.isFeedback ? " [feedback]" : "") + ">";
              });

    py::class_<Graph_t> (m, "AudioGraph")
        .def (py::init<>())
        .def (
            "remove_node",
            [] (Graph_t& self, NodeId id)
            {
                auto result = self.removeNode (id);
                if (! result.has_value())
                {
                    throw py::value_error (
                        std::string ("remove_node: ")
                        + (result.error() == GraphError::NodeNotFound ? "node not found" : "unknown error"));
                }
            },
            py::arg ("id"))

        .def (
            "connect",
            [] (Graph_t& self,
                NodeId src,
                std::size_t srcPort,
                NodeId dst,
                std::size_t dstPort,
                ConnectionType type,
                bool isFeedback)
            {
                auto result = self.connect (src, srcPort, dst, dstPort, type, isFeedback);
                if (! result.has_value())
                {
                    const auto e    = result.error();
                    std::string msg = "connect: ";
                    switch (e)
                    {
                        case GraphError::InvalidNodeId:
                            msg += "invalid node id";
                            break;
                        case GraphError::InvalidPort:
                            msg += "port out of range";
                            break;
                        case GraphError::DuplicateConnection:
                            msg += "duplicate connection";
                            break;
                        case GraphError::TypeMismatch:
                            msg += "connection type mismatch";
                            break;
                        default:
                            msg += "unknown error";
                            break;
                    }
                    throw py::value_error (msg);
                }
            },
            py::arg ("src_node"),
            py::arg ("src_port"),
            py::arg ("dst_node"),
            py::arg ("dst_port"),
            py::arg ("connection_type") = ConnectionType::Audio,
            py::arg ("is_feedback")     = false)

        .def (
            "disconnect",
            [] (Graph_t& self,
                NodeId src,
                std::size_t srcPort,
                NodeId dst,
                std::size_t dstPort,
                ConnectionType type,
                bool isFeedback)
            {
                auto result = self.disconnect (src, srcPort, dst, dstPort, type, isFeedback);
                if (! result.has_value())
                {
                    throw py::value_error ("disconnect: connection not found");
                }
            },
            py::arg ("src_node"),
            py::arg ("src_port"),
            py::arg ("dst_node"),
            py::arg ("dst_port"),
            py::arg ("connection_type") = ConnectionType::Audio,
            py::arg ("is_feedback")     = false)

        .def (
            "prepare",
            [] (Graph_t& self, std::size_t numChannels, std::size_t numFrames, double sampleRate)
            {
                auto result = self.prepare (numChannels, numFrames, sampleRate);
                if (! result.has_value())
                {
                    if (result.error() == GraphError::CycleDetected)
                    {
                        throw py::value_error ("prepare: cycle detected — mark a back-edge is_feedback=True");
                    }
                    throw py::value_error ("prepare: failed");
                }
            },
            py::arg ("num_channels"),
            py::arg ("num_frames"),
            py::arg ("sample_rate"))

        .def ("process", &Graph_t::process)
        .def ("is_prepared", &Graph_t::isPrepared)
        .def ("get_num_nodes", &Graph_t::getNumNodes)
        .def ("get_num_connections", &Graph_t::getNumConnections)
        .def ("get_sorted_order", &Graph_t::getSortedOrder)

        .def (
            "get_node_type",
            [] (const Graph_t& self, NodeId id) -> NodeType
            {
                const auto* node = self.getNode (id);
                if (! node)
                {
                    throw py::value_error ("get_node_type: node not found");
                }
                return node->getType();
            },
            py::arg ("id"))

        .def (
            "get_num_input_ports",
            [] (const Graph_t& self, NodeId id) -> std::size_t
            {
                const auto* node = self.getNode (id);
                if (! node)
                {
                    throw py::value_error ("get_num_input_ports: node not found");
                }
                return node->getNumInputPorts();
            },
            py::arg ("id"))

        .def (
            "get_num_output_ports",
            [] (const Graph_t& self, NodeId id) -> std::size_t
            {
                const auto* node = self.getNode (id);
                if (! node)
                {
                    throw py::value_error ("get_num_output_ports: node not found");
                }
                return node->getNumOutputPorts();
            },
            py::arg ("id"))

        .def (
            "get_sample_rate",
            [] (const Graph_t& self, NodeId id) -> float
            {
                const auto* node = self.getNode (id);
                if (! node)
                {
                    throw py::value_error ("get_sample_rate: node not found");
                }
                return node->getSampleRate();
            },
            py::arg ("id"))

        .def (
            "node_is_prepared",
            [] (const Graph_t& self, NodeId id) -> bool
            {
                const auto* node = self.getNode (id);
                if (! node)
                {
                    throw py::value_error ("node_is_prepared: node not found");
                }
                return node->isPrepared();
            },
            py::arg ("id"))

        // Non-owning raw pointer — valid only while the graph is alive
        // and the node has not been removed.
        .def ("add_node",
        [] (Graph_t& self, std::shared_ptr<NodeBase_t> node) -> NodeId
    {
        if (!node)
        {
            throw py::value_error("add_node: node must not be None");
        }

        // Release from shared_ptr and give unique ownership to the graph.
        // Python's shared_ptr wrapper becomes a dangling alias after this.
        auto result = self.addNode(std::unique_ptr<NodeBase_t>(node.get()));
        node.reset(); // clear the shared_ptr so it doesn't double-free

        if (!result.has_value())
        {
            throw py::value_error("add_node: failed");
        }

        return result.value();
    },
    py::arg("node"),
    py::return_value_policy::reference,
            R"pbdoc(
                  Add a node to the graph, transferring ownership.
                  Returns the assigned NodeId.

                  IMPORTANT: Do not use the node object after calling add_node().
                  The graph owns the node. Store the returned NodeId instead.

                  Example:
                      env_id = graph.add_node(caspi.adsr.ADSR())
                      # env_id is valid; the ADSR() object is now owned by graph
              )pbdoc");
}