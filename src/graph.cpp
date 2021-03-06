// Copyright 2017 Nest Labs, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "graph.hpp"

#include "dglogging.hpp"
#include "dgassert.hpp"

namespace DetectorGraph
{

Graph::Graph()
#if !defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
 : mNeedsSorting(false)
#endif
{
    DG_LOG("Graph Initialized");
}

Graph::~Graph()
{
#if !defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    // Detectors remove themselves from the graph when deleted
    // so we need a robust deletion process (instead of for (begin,!=end,++))
    // that is ok with removing items behind the iterator.
    std::list<Vertex*>::iterator vertexIt = mVertices.begin();
    while (vertexIt != mVertices.end())
    {
        Vertex* tmp = *vertexIt;
        vertexIt++;
        if (tmp->GetVertexType() == Vertex::kDetectorVertex)
        {
            delete tmp;
        }
    }

    vertexIt = mVertices.begin();
    while (vertexIt != mVertices.end())
    {
        Vertex* tmp = *vertexIt;
        vertexIt++;
        delete tmp;
    }
#endif
}

void Graph::AddVertex(Vertex* aVertex)
{
    mVertices.push_back(aVertex);
#if !defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    mNeedsSorting = true;
#endif
#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_INSTRUMENT_RESOURCE_USAGE)
    DG_LOG("Added Vertex (total=%u)", mVertices.size());
#endif
}

void Graph::RemoveVertex(Vertex* aVertex)
{
#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    // TODO(cscotti): Assert? Destructor? Linear remove?
    DG_LOG("dummy RemoveVertex called for %p.", (void*)aVertex);
#else
    mVertices.remove(aVertex);
    mNeedsSorting = true;
#endif
}

bool Graph::EvaluateIfHasDataPending()
{
    if (HasDataPending())
    {
        ErrorType r = ErrorType_Success;
        r = EvaluateGraph();
        if (r != ErrorType_Success) // LCOV_EXCL_START
        {
            DG_LOG("Evaluation failed with %d.", (int)r);
            DG_ASSERT(false);
        } // LCOV_EXCL_STOP
        return true;
    }

    return false;
}

bool Graph::HasDataPending()
{
    return !mGraphInputQueue.IsEmpty();
}

TopicRegistry& Graph::GetTopicRegistry()
{
    return mTopicRegistry;
}

ErrorType Graph::ClearTraverseContexts()
{
    ErrorType r = ErrorType_Success;
    for (VertexPtrContainer::iterator vertexIt = mVertices.begin();
    vertexIt != mVertices.end();
    ++vertexIt)
    {
        (*vertexIt)->SetState(Vertex::kVertexClear);
    }
    return r;
}

ErrorType Graph::TraverseVertices()
{
    ErrorType r = ErrorType_Success;

    for (VertexPtrContainer::iterator it = mVertices.begin();
        it != mVertices.end();
        ++it)
    {
        (*it)->ProcessVertex();
    }

    return r;
}

ErrorType Graph::EvaluateGraph()
{
    ErrorType r = ErrorType_Success;

#if !defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    if (mNeedsSorting)
    {
        r = TopoSortGraph();
        if (r != ErrorType_Success)
        {
            DG_LOG("Graph::TopoSortGraph() failed");
            return r;
        }
    }
#endif

    ClearTraverseContexts();

    mGraphInputQueue.DequeueAndDispatch();

    r = TraverseVertices();
    if (r != ErrorType_Success) // LCOV_EXCL_START // Dead code for future-proofness
    {
        DG_LOG("Graph::TraverseVertices() failed");
        return r;
    } // LCOV_EXCL_STOP

#if !defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
    r = ComposeOutputList();
    if (r != ErrorType_Success) // LCOV_EXCL_START // Dead code for future-proofness
    {
        DG_LOG("Graph::ComposeOutputList() failed");
        return r;
    } // LCOV_EXCL_STOP
#endif

    return r;
}

#if defined(BUILD_FEATURE_DETECTORGRAPH_CONFIG_LITE)
bool Graph::IsGraphSorted()
{
    ClearTraverseContexts();
    int processingNodes = 0;
    for (VertexPtrContainer::iterator vertexIt = mVertices.begin();
        vertexIt != mVertices.end();
        ++vertexIt)
    {
        Vertex* v = *vertexIt;
        if (v->GetState() == Vertex::kVertexDone)
        {
            return false;
        }
        else if (v->GetState() == Vertex::kVertexClear)
        {
            processingNodes++;
            v->SetState(Vertex::kVertexProcessing);
        }
        for (VertexPtrContainer::iterator outEdgeIt = v->GetOutEdges().begin();
            outEdgeIt != v->GetOutEdges().end();
            ++outEdgeIt)
        {
            Vertex* outVertex = *outEdgeIt;
            if (outVertex->GetState() == Vertex::kVertexDone)
            {
                return false;
            }
            else if (outVertex->GetState() == Vertex::kVertexClear)
            {
                processingNodes++;
                outVertex->SetState(Vertex::kVertexProcessing);
            }
        }

        v->SetState(Vertex::kVertexDone);
        processingNodes--;
    }

    if (processingNodes != 0)
    {
        DG_LOG("---------%d Out of Bounds edge --------", processingNodes);
        return false;
    }

    return true;
}
#else
// FULL_BEGIN

ErrorType Graph::TopoSortGraph()
{
    ErrorType r = ErrorType_Success;
    // SORT

    // Clean graph-search context
    ClearTraverseContexts();

    std::list<Vertex*> sorted;

    size_t numVertices = mVertices.size();

    // cout << "--- TOPOSORT START ---" << endl;
    // DFS starting on any random vertex and repeating for other undiscovered
    // vertices
    for (std::list<Vertex*>::iterator vertexIt = mVertices.begin();
        vertexIt != mVertices.end();
        ++vertexIt)
    {
        if ((*vertexIt)->GetState() == Vertex::kVertexClear)
        {
            // cout << "TOP DFS_visit" << endl;
            r = DFS_visit(*vertexIt, sorted);
            if (r != ErrorType_Success)
            {
                return r;
            }
        }
    }
    // cout << "--- TOPOSORT END ---" << endl;

    if (sorted.size() != numVertices)
    {
        // Tree structure is inconsistent.
        // This probably means that a vertex 'm' pointed to
        // a vertex 'n' outside of the graph. This can happen
        // if you have a bug in the insertion/removal of detectors.
        DG_LOG("--------------------------------------");
        DG_LOG("------------- BAD GRAPH --------------");
        DG_LOG("---------- Out of Bounds edge --------");
        DG_LOG("--------------------------------------");
        r = ErrorType_BadConfiguration;
        return r;
    }

    // Save sorted vertices
    mVertices = sorted;
    mNeedsSorting = false;

    return r;
}

ErrorType Graph::DFS_visit(Vertex* v, std::list<Vertex*>& sorted)
{
    ErrorType r = ErrorType_Success;

    // cout << "---> Starting at " << v->GetName() << " that has " << v->GetOutEdges().size() << " out edges " << endl;
    v->SetState(Vertex::kVertexProcessing);
    for (std::list<Vertex*>::iterator vIt = v->GetOutEdges().begin();
        vIt != v->GetOutEdges().end();
        ++vIt)
    {
        // cout << "Examining edge from " << v->GetName() << " to " << (*vIt)->GetName() << endl;
        if ((*vIt)->GetState() == Vertex::kVertexClear)
        {
            r = DFS_visit(*vIt, sorted);
            if (r != ErrorType_Success)
            {
                return r;
            }
        }
        else if ((*vIt)->GetState() == Vertex::kVertexProcessing)
        {
            DG_LOG("--------------------------------------");
            DG_LOG("------------CYCLE DETECTED------------");
            DG_LOG("---------- Will Ignore edge ----------");
            DG_LOG("--------------------------------------");
            DG_LOG("Cycle at: %s", (*vIt)->GetName());
            r = ErrorType_BadConfiguration;
            if (r != ErrorType_Success)
            {
                return r;
            }
        } // LCOV_EXCL_LINE
        //v2 - do nothing.
    }
    // cout << "<--- Finished at " << v->GetName() << endl;
    v->SetState(Vertex::kVertexDone);
    sorted.push_front(v);

    return r;
}

ErrorType Graph::ComposeOutputList()
{
    ErrorType r = ErrorType_Success;

    mOutputList.clear();

    for (std::list< Vertex* >::iterator it = mVertices.begin();
        it != mVertices.end();
        ++it)
    {
        if ( (*it)->GetVertexType() == Vertex::kTopicVertex)
        {
            BaseTopic* tTopic = static_cast<BaseTopic*>(*it);
            std::list< ptr::shared_ptr<const TopicState> > tTopicStates = tTopic->GetCurrentTopicStates();

            //Pushes back entire list
            mOutputList.insert(mOutputList.end(), tTopicStates.begin(), tTopicStates.end());
        }
    }

    return r;
} // LCOV_EXCL_LINE

const std::list< ptr::shared_ptr<const TopicState> >& Graph::GetOutputList() const
{
    return mOutputList;
}

// FULL_END
#endif

}
