/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include "absl/container/flat_hash_set.h"
#include "tfdml/optimizer/graph_view_internal.h"
#include "tfdml/runtime_adapter/status.h"

namespace tensorflow
{
class NodeDef;
}

namespace tfdml
{

class NodeView;

class GraphView;

std::string AsControlDependency(const std::string& node_name);

// FaninView is a helper class to represent fanouts of a node. This holds a
// pointer to GraphView, the index of the node being represented from GraphView,
// and the input index (hence is labeled as Fanin).
class FaninView : public NodeIndexAndPortIndex<NodeView, GraphView>
{
  public:
    FaninView() : NodeIndexAndPortIndex() {}

    FaninView(GraphView* graph_view, int node_index, int port_index)
        : NodeIndexAndPortIndex(graph_view, node_index, port_index)
    {
    }

    FaninView(NodeView* node_view, int index);

  private:
    friend class NodeView;
    friend class GraphView;
};

// FanoutView is a helper class to represent fanins of a node. This holds a
// pointer to GraphView, the index of the node being represented from GraphView,
// and the output index (hence is labeled as Fanout).
class FanoutView : public NodeIndexAndPortIndex<NodeView, GraphView>
{
  public:
    FanoutView() : NodeIndexAndPortIndex() {}

    FanoutView(GraphView* graph_view, int node_index, int port_index)
        : NodeIndexAndPortIndex(graph_view, node_index, port_index)
    {
    }

    FanoutView(NodeView* node_view, int index);

  private:
    friend class NodeView;
    friend class GraphView;
};

// Immutable NodeView that keeps the constness of the NodeDef. This allows for
// lookups of fanins and fanouts, and traversals of the graph, but no mutations.
// No dedupping of fanins will be performed on the node to preserve it's
// constness.
class NodeView : public NodeViewInternal<FaninView, FanoutView, GraphView, true>
{
  public:
    explicit NodeView(GraphView* graph_view, int node_index)
        : NodeViewInternal(graph_view, node_index)
    {
    }

    NodeView() : NodeViewInternal() {}

    ~NodeView() override = default;

    NodeView(NodeView&&) = default;
    NodeView& operator=(NodeView&&) = default;

    const tensorflow::NodeDef* node() const override;

    // Checks if a fanin exists for the node.
    bool HasFanin(const FanoutView& fanin) const override;

    // Checks if a fanout exists for the node.
    bool HasFanout(const FaninView& fanout) const override;

  private:
    inline const FanoutView& GetMissingFanin() const override;

    inline const std::vector<FaninView>& GetMissingFanout() const override;

    absl::flat_hash_set<NodeDefAndPortIndex> fanins_set_;

    friend class FaninView;
    friend class FanoutView;
    friend class GraphView;
};

// Immutable GraphView that keeps the constness of the GraphDef. This allows
// for lookups and traversals of the graph, but no mutations.
class GraphView
    : public GraphViewInternal<NodeView, FaninView, FanoutView, true>
{
  public:
    explicit GraphView(const tensorflow::GraphDef* graph, Status* status);
    ~GraphView() override = default;

  private:
    bool AddUniqueNodeInternal(const tensorflow::NodeDef* node);

    Status CheckAndAddFaninsInternal(NodeView* node_view);

    friend class NodeView;
};

class MutableNodeView;

class MutableGraphView;

class Mutation;

// MutableFaninView is a helper class to represent fanouts of a node. This holds
// a pointer to MutableGraphView, the index of the node from MutableGraphView
// being mutated, and the input index (hence is labeled as Fanin).
class MutableFaninView
    : public NodeIndexAndPortIndex<MutableNodeView, MutableGraphView>
{
  public:
    MutableFaninView() : NodeIndexAndPortIndex() {}

    MutableFaninView(
        MutableGraphView* graph_view,
        int node_index,
        int port_index)
        : NodeIndexAndPortIndex(graph_view, node_index, port_index)
    {
    }

    explicit MutableFaninView(
        MutableGraphView* graph_view,
        int node_index,
        int port_index,
        int fanin_index)
        : NodeIndexAndPortIndex(graph_view, node_index, port_index),
          fanin_index_(fanin_index)
    {
        // TODO(lyandy): Remove once constructor is not public.
        assert(port_index < 0 || port_index == fanin_index);
    }

    MutableFaninView(MutableNodeView* node_view, int index);

  private:
    // Index of associated fanin in fanout's underlying MutableNodeView. For
    // regular fanouts, this will be the same as port_index (index of the
    // associated fanin in MutableNodeView::regular_fanins_). For controlled
    // fanouts, this will be the index of the associated fanin in
    // MutableNodeView::controlling_fanins_.
    int fanin_index_ = kMissingIndex;

    friend class MutableNodeView;
    friend class MutableGraphView;
    friend class Mutation;
};

// MutableFanoutView is a helper class to represent fanins of a node. This holds
// a pointer to MutableGraphView, the index of the node from MutableGraphView
// being mutated, and the output index (hence is labeled as Fanout).
class MutableFanoutView
    : public NodeIndexAndPortIndex<MutableNodeView, MutableGraphView>
{
  public:
    MutableFanoutView() : NodeIndexAndPortIndex() {}

    MutableFanoutView(
        MutableGraphView* graph_view,
        int node_index,
        int port_index)
        : NodeIndexAndPortIndex(graph_view, node_index, port_index)
    {
    }

    explicit MutableFanoutView(
        MutableGraphView* graph_view,
        int node_index,
        int port_index,
        int fanout_index)
        : NodeIndexAndPortIndex(graph_view, node_index, port_index),
          fanout_index_(fanout_index)
    {
    }

    MutableFanoutView(MutableNodeView* node_view, int index);

  private:
    // Index of associated fanout in fanin's underlying MutableNodeView. For
    // regular fanins, this will be the index of the associated fanout in
    // MutableNodeView::regular_fanouts_by_port_[port_index]. For controlled
    // fanins, this will be the index of the associated fanout in
    // MutableNodeView::controlled_fanouts_.
    int fanout_index_ = kMissingIndex;

    friend class MutableNodeView;
    friend class MutableGraphView;
    friend class Mutation;
};

// Mutable NodeView that holds a mutable NodeDef. This allows for lookups of
// fanins and fanouts, and traversals of the graph. Control dependencies will be
// dedupped among other control dependencies on initialization via
// MutableGraphView. Mutations should be handled via MutableGraphView and not
// directly on the mutable NodeDef.
class MutableNodeView : public NodeViewInternal<
                            MutableFaninView,
                            MutableFanoutView,
                            MutableGraphView,
                            false>
{
  public:
    explicit MutableNodeView(MutableGraphView* graph_view, int node_index)
        : NodeViewInternal(graph_view, node_index)
    {
    }

    MutableNodeView() : NodeViewInternal() {}

    ~MutableNodeView() override = default;

    MutableNodeView(MutableNodeView&&) = default;
    MutableNodeView& operator=(MutableNodeView&&) = default;

    tensorflow::NodeDef* node() const override;

    // Checks if a fanin exists for the node.
    bool HasFanin(const MutableFanoutView& fanin) const override;

    // Checks if a fanout exists for the node.
    bool HasFanout(const MutableFaninView& fanout) const override;

  private:
    inline const MutableFanoutView& GetMissingFanin() const override;

    inline const std::vector<MutableFaninView>& GetMissingFanout()
        const override;

    absl::flat_hash_map<NodeDefAndPortIndex, int> fanins_count_;
    absl::flat_hash_map<absl::string_view, int> controlling_fanins_index_;
    // Index of associated MutableNodeViewDiff in Mutation::updated_nodes_.
    // If this is -1, there exists no MutableNodeViewDiff for this node.
    int update_index_ = kMissingIndex;

    friend class MutableFaninView;
    friend class MutableFanoutView;
    friend class MutableGraphView;
    friend class Mutation;
};

class MutationNewNode
{
  public:
    MutationNewNode() {}

  private:
    explicit MutationNewNode(
        Mutation* mutation,
        int mutation_counter,
        int index)
        : mutation_(mutation),
          mutation_counter_(mutation_counter),
          index_(index)
    {
    }

    Mutation* mutation_ = nullptr;
    int mutation_counter_ = kMissingSlot;
    int index_ = kMissingIndex;

    friend class Mutation;
};

// Mutation is a helper class that allows rewrites of MutableGraphView. This
// should not be initialized or be used directly.
// Note, if a node is renamed to another node, or a new node is created with the
// same name as an existing node, the node with the same name originally in the
// graph will be overwritten.
class Mutation
{
  public:
    // Create a new node to be added to the graph. If the node's fanins are not
    // well formed (self loops, control dependencies between regular fanins),
    // the `status` will be set.
    MutationNewNode AddNode(tensorflow::NodeDef&& node, Status* status);

    // Remove an existing node in the graph.
    void RemoveNode(MutableNodeView* node);

    // Update the name of an existing node.
    void UpdateNodeName(MutableNodeView* node, absl::string_view name);

    // Update the name of a new node.
    void UpdateNodeName(const MutationNewNode& node, absl::string_view name);

    // Update the op of an existing node.
    void UpdateNodeOp(MutableNodeView* node, absl::string_view op);

    // Update the op of a new node.
    void UpdateNodeOp(const MutationNewNode& node, absl::string_view op);

    // Update the device of an existing node.
    void UpdateNodeDevice(MutableNodeView* node, absl::string_view device);

    // Update the device of a new node.
    void UpdateNodeDevice(
        const MutationNewNode& node,
        absl::string_view device);

    // Add or replace regular fanin `fanin` at `index` for an existing node.
    void AddOrUpdateRegularFanin(
        MutableNodeView* node,
        int index,
        const TensorId& fanin);

    // Add or replace regular fanin `fanin` at `index` for a new node.
    void AddOrUpdateRegularFanin(
        const MutationNewNode& node,
        int index,
        const TensorId& fanin);

    // Remove regular fanin at `index` for an existing node.
    void RemoveRegularFanin(MutableNodeView* node, int index);

    // Remove regular fanin at `index` for a new node.
    void RemoveRegularFanin(const MutationNewNode& node, int index);

    // Add controlling fanin `fanin_node_name` for an existing node.
    void AddControllingFanin(
        MutableNodeView* node,
        absl::string_view fanin_node_name);

    // Add controlling fanin `fanin_node_name` for a new node.
    void AddControllingFanin(
        const MutationNewNode& node,
        absl::string_view fanin_node_name);

    // Remove controlling fanin `fanin_node_name` for an existing node.
    void RemoveControllingFanin(
        MutableNodeView* node,
        absl::string_view fanin_node_name);

    // Remove controlling fanin `fanin_node_name` for a new node.
    void RemoveControllingFanin(
        const MutationNewNode& node,
        absl::string_view fanin_node_name);

    // Add or replace attribute `attr_name` with `attr_value` for an existing
    // node.
    void AddOrUpdateNodeAttr(
        MutableNodeView* node,
        absl::string_view attr_name,
        const tensorflow::AttrValue& attr_value);

    // Add or replace attribute `attr_name` with `attr_value` for a new node.
    void AddOrUpdateNodeAttr(
        const MutationNewNode& node,
        absl::string_view attr_name,
        const tensorflow::AttrValue& attr_value);

    // Remove attribute `attr_name` for an existing node.
    void RemoveNodeAttr(MutableNodeView* node, absl::string_view attr_name);

    // Remove attribute `attr_name` for a new node.
    void RemoveNodeAttr(
        const MutationNewNode& node,
        absl::string_view attr_name);

    // Reset and clear mutation.
    void Reset();

    // Applies the Mutation to the graph. If the mutation is valid, the graph
    // will be modified. Otherwise an error status will be returned and the
    // graph will not be modified.
    Status Apply();

  private:
    explicit Mutation(MutableGraphView* graph_view);

    void ResetInternal();

    using MutableNodeViewDiff = NodeViewDiff<MutableGraphView>;

    // Adds a mutation to the `node`. Mutation function `mutate_fn` must return
    // `true` if it actually does any mutations. If it returns `false` mutation
    // will be ignored.
    void AddMutation(
        MutableNodeView* node,
        std::function<bool(MutableNodeViewDiff*)> mutate_fn);

    MutableGraphView* graph_view_ = nullptr;
    int mutation_counter_ = 0;
    std::vector<MutableNodeViewDiff> updated_nodes_;
    absl::flat_hash_set<int> removed_nodes_;

    using MutationNewNodeHolder = NewNode<MutableGraphView>;
    std::vector<MutationNewNodeHolder> new_nodes_;

    friend class MutableGraphView;
};

// Mutable GraphView that holds a mutable GraphDef. This allows for lookups and
// traversals of the graph. Control dependencies will be dedupped among other
// control dependencies on initialization. Mutations should be handled using
// this API instead of directly on the GraphDef/NodeDef.
// Note, after a mutation, pointers of MutableNodeView's from MutableGraphView
// may be invalidated.
class MutableGraphView : public GraphViewInternal<
                             MutableNodeView,
                             MutableFaninView,
                             MutableFanoutView,
                             false>
{
  public:
    explicit MutableGraphView(tensorflow::GraphDef* graph, Status* status);
    ~MutableGraphView() override = default;

    // Returns a Mutation (builder) that can be used to modify MutableGraphView.
    Mutation* GetMutationBuilder();

    // Helper class representing an extra dependency for topological sorting.
    class TopologicalDependency
    {
      public:
        TopologicalDependency(
            const MutableNodeView* from_node,
            const MutableNodeView* to_node)
        {
            if (from_node->graph_view_ == to_node->graph_view_)
            {
                graph_view_ = from_node->graph_view_;
                from_ = from_node->node_index_;
                to_ = to_node->node_index_;
            }
        }

      private:
        MutableGraphView* graph_view_ = nullptr;
        int from_ = kMissingIndex;
        int to_ = kMissingIndex;

        friend class MutableGraphView;
    };

    // Sorts graph topologically in-place. If `ignore_cycles` is set, a
    // topological like sorting will be performed when there are cycles.
    // Otherwise if a cycle is detected or if the graph cannot be sorted, an
    // error will be returned.
    Status SortTopologically(
        bool ignore_cycles,
        absl::Span<const TopologicalDependency> extra_dependencies);

  private:
    bool AddUniqueNodeInternal(tensorflow::NodeDef* node);

    Status CheckFaninsInternal(std::vector<std::vector<TensorId>>* fanins);

    void AddFaninsInternal(std::vector<std::vector<TensorId>>* fanins);

    // RenamedOrOverwrittenNode holds a index to Mutation::updated_nodes_ for a
    // renamed node, alongside a potential overwritten node index in the actual
    // graph. If the renamed node is not overwriting any existing nodes,
    // `overwritten_node_index_` will be set to `kMissingIndex`.
    class RenamedOrOverwrittenNode
    {
      public:
        RenamedOrOverwrittenNode(
            int renamed_update_index,
            int overwritten_node_index)
            : renamed_update_index_(renamed_update_index),
              overwritten_node_index_(overwritten_node_index)
        {
        }

      private:
        int renamed_update_index_;
        int overwritten_node_index_;

        friend class MutableGraphView;
    };

    Status GetNodeNamesAndPartitionUpdatedNodes(
        absl::flat_hash_map<absl::string_view, int>* node_names,
        std::vector<RenamedOrOverwrittenNode>* renamed_nodes,
        std::vector<int>* inplace_nodes,
        std::vector<int>* empty_diff_node_indices);

    Status RemovedOrMissingNodeFanoutsWellFormed(
        const absl::flat_hash_map<absl::string_view, int>& node_names,
        const std::vector<RenamedOrOverwrittenNode>& renamed_nodes);

    Status CheckNodeNamesAndFanins(
        const absl::flat_hash_map<absl::string_view, int>& node_names,
        const std::vector<RenamedOrOverwrittenNode>& renamed_nodes,
        const std::vector<int>& inplace_nodes);

    Status CheckKernelRegisteredForNodes();

    // Helper class to move fanouts around.
    class NodeViewFanouts
    {
      public:
        NodeViewFanouts(
            std::vector<std::vector<MutableFaninView>>&&
                regular_fanouts_by_port,
            int num_regular_fanouts,
            std::vector<MutableFaninView> controlled_fanouts)
            : regular_fanouts_by_port_(std::move(regular_fanouts_by_port)),
              num_regular_fanouts_(num_regular_fanouts),
              controlled_fanouts_(std::move(controlled_fanouts))
        {
        }

      private:
        std::vector<std::vector<MutableFaninView>> regular_fanouts_by_port_;
        int num_regular_fanouts_ = 0;
        std::vector<MutableFaninView> controlled_fanouts_;

        friend class MutableGraphView;
    };

    template <typename T>
    void ReplaceNodeFanouts(MutableNodeView* node, T* fanouts);

    void FixRenamedNodes(
        std::vector<RenamedOrOverwrittenNode>* renamed_nodes,
        absl::flat_hash_map<std::string, NodeViewFanouts>* renamed_fanouts,
        std::vector<bool>* overwritten_name_removed_nodes);

    void AddNewNodes(
        absl::flat_hash_map<std::string, NodeViewFanouts>* renamed_fanouts,
        std::vector<int>* new_node_indices);

    void FixRenamedFanouts(
        const absl::flat_hash_map<std::string, NodeViewFanouts>&
            renamed_fanouts);

    inline void RemoveRegularFaninFanoutInternal(
        MutableNodeView* node_view,
        int i);

    inline void AddRegularFaninInternal(
        MutableNodeView* node_view,
        const SafeTensorId& fanin_id);

    inline void UpdateRegularFaninInternal(
        MutableNodeView* node_view,
        const int i,
        const SafeTensorId& fanin_id);

    inline void RemoveControllingFaninFanoutInternal(
        MutableNodeView* node_view,
        int i);

    inline void RemoveControllingFaninInternal(
        MutableNodeView* node_view,
        const std::set<int>& indices_to_remove);

    inline void AddControllingFaninInternal(
        MutableNodeView* node_view,
        absl::string_view fanin_node_name);

    void ApplyNodeUpdates();

    void SetNewNodesFanins(const std::vector<int>& new_node_indices);

    inline void RemoveAllFaninFanoutInternal(MutableNodeView* node_view);

    void RemoveNodesInternal(
        const std::vector<RenamedOrOverwrittenNode>& renamed_nodes,
        const std::vector<bool>& overwritten_name_removed_nodes);

    inline Status ValidateInternal(
        absl::flat_hash_map<absl::string_view, int>* node_names,
        std::vector<RenamedOrOverwrittenNode>* renamed_nodes,
        std::vector<int>* inplace_nodes,
        std::vector<int>* empty_diff_node_indices);

    Status ApplyMutationInternal();

    Mutation mutation_;

    friend class MutableNodeView;
    friend class Mutation;
};

} // namespace tfdml
