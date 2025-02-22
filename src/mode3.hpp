#ifndef SHASTA_MODE3_HPP
#define SHASTA_MODE3_HPP

/*******************************************************************************

Class mode3::AssemblyGraph is the class used for Mode 3 assembly.
Using GFA terminology, the graph consists of Segments and Links.

A Segment corresponds to a linear sequence of edges, without branches,
in the marker graph.

If an oriented read enters segment 1 immediately after exiting segment 0,
we say that there is a transition 0->1. If there is a sufficient
number of transitions 0->1, we create a link 0->1.

*******************************************************************************/

// Shasta.
#include "hashArray.hpp"
#include "MemoryMappedVectorOfVectors.hpp"
#include "MultithreadedObject.hpp"
#include "ReadId.hpp"
#include "shastaTypes.hpp"

// Boost libraries.
#include <boost/graph/adjacency_list.hpp>

// Standard library.
#include "array.hpp"
#include "unordered_map"
#include "vector.hpp"



namespace shasta {
    namespace mode3 {
        class AssemblyGraph;
    }

    // Some forward declarations of classes in the shasta namespace.
    class CompressedMarker;
    class MarkerGraph;
}



// The AssemblyGraph is used to store the Mode 3 assembly graph,
// when it no longer needs to be changed,
// in memory mapped data structures.
class shasta::mode3::AssemblyGraph :
    public MultithreadedObject<AssemblyGraph> {
public:

    // Initial construction.
    AssemblyGraph(
        const string& largeDataFileNamePrefix,
        size_t largeDataPageSize,
        size_t threadCount,
        const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers,
        const MarkerGraph&);

    // Constructor from binary data.
    AssemblyGraph(
        const string& largeDataFileNamePrefix,
        const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers,
        const MarkerGraph&);

    // Data and functions to handle memory mapped data.
    const string& largeDataFileNamePrefix;
    size_t largeDataPageSize;
    string largeDataName(const string&) const;
    template<class T> void createNew(T& t, const string& name)
    {
        t.createNew(largeDataName(name), largeDataPageSize);
    }
    template<class T> void accessExistingReadOnly(T& t, const string& name)
    {
        t.accessExistingReadOnly(largeDataName(name));
    }

    // References to Assembler objects.
    const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers;
    const MarkerGraph& markerGraph;

    // Each  linear chain of marker graph edges generates a segment.
    // The marker graph path corresponding to each segment is stored
    // indexed by segment id.
    MemoryMapped::VectorOfVectors<MarkerGraphEdgeId, uint64_t> paths;
    void createSegmentPaths();

    // Average marker graph edge coverage for all segments.
    MemoryMapped::Vector<float> segmentCoverage;
    void computeSegmentCoverage();

    // Keep track of the segment and position each marker graph edge corresponds to.
    // For each marker graph edge, store in the marker graph edge table
    // the corresponding segment id and position in the path, if any.
    // Indexed by the edge id in the marker graph.
    // This is needed when computing pseudopaths.
    MemoryMapped::Vector< pair<uint64_t, uint32_t> > markerGraphEdgeTable;
    void computeMarkerGraphEdgeTable(size_t threadCount);
    void computeMarkerGraphEdgeTableThreadFunction(size_t threadId);



    // Pseudopaths for all oriented reads.
    // The pseudopath of an oriented read is the sequence of
    // marker graph edges it encounters.
    // For each entry in the pseudopath, the marker graph
    // edge is identified by the segmentId in the AssemblyGraph
    // and the edge position in the segment (e. g. the first marker graph edge
    // in the segment is at position 0).
    // Indexed by OrientedReadId::getValue().
    // It gets removed when no longer needed.
    class PseudoPathEntry {
    public:
        uint64_t segmentId;
        uint32_t position;
        array<uint32_t, 2> ordinals;

        bool operator<(const PseudoPathEntry& that) const
        {
            return ordinals[0] < that.ordinals[0];
        }
        bool operator==(const PseudoPathEntry& that) const
        {
            return
                tie(segmentId, position, ordinals) ==
                tie(that.segmentId, that.position, that.ordinals);
        }
    };
    MemoryMapped::VectorOfVectors<PseudoPathEntry, uint64_t> pseudoPaths;
    void computePseudoPaths(size_t threadCount);
    void computePseudoPathsPass1(size_t threadId);
    void computePseudoPathsPass2(size_t threadId);
    void computePseudoPathsPass12(uint64_t pass);
    void sortPseudoPaths(size_t threadId);



    // The compressed pseudopath of an oriented read
    // is the sequence of segmentIds it encounters.
    // It stores only the first and last PseudoPathEntry
    // on each segment.
    // Note a segmentId can appear more than once on the compressed
    // pseudopath of an oriented read.
    class CompressedPseudoPathEntry {
    public:
        uint64_t segmentId;

        // The first and last PseudoPathEntry's that contributed to this
        // CompressedPseudoPathEntry.
        array<PseudoPathEntry, 2> pseudoPathEntries;
    };
    // Indexed by OrientedReadId::getValue().
    MemoryMapped::VectorOfVectors<CompressedPseudoPathEntry, uint64_t> compressedPseudoPaths;
    void computeCompressedPseudoPaths();
    void computeCompressedPseudoPath(
        const span<PseudoPathEntry> pseudoPath,
        vector<CompressedPseudoPathEntry>& compressedPseudoPath);

    // Store appearances of segments in compressed pseudopaths.
    // For each segment, store pairs (orientedReadId, position in compressed pseudo path).
    MemoryMapped::VectorOfVectors<pair<OrientedReadId, uint64_t>, uint64_t> segmentCompressedPseudoPathInfo;
    void computeSegmentCompressedPseudoPathInfo();



    // A pseudopath transition occurs when the pseudopath of an oriented read
    // moves from a segment to a different segment.
    // Transitions are used to create edges in the AssemblyGraph (gfa links).
    class Transition : public array<PseudoPathEntry, 2> {
    public:
        Transition(const array<PseudoPathEntry, 2>& x) : array<PseudoPathEntry, 2>(x) {}
        Transition() {}
    };

    // Find pseudopath transitions and store them keyed by the pair of segments.
    using SegmentPair = pair<uint64_t, uint64_t>;
    using Transitions = vector< pair<OrientedReadId, Transition> >;
    std::map<SegmentPair, Transitions> transitionMap;
    void findTransitions(std::map<SegmentPair, Transitions>& transitionMap);



    // The links.
    class Link {
    public:
        uint64_t segmentId0;
        uint64_t segmentId1;

        Link(
            uint64_t segmentId0 = 0,
            uint64_t segmentId1 = 0,
            uint64_t coverage = 0) :
            segmentId0(segmentId0),
            segmentId1(segmentId1) {}
    };
    MemoryMapped::Vector<Link> links;
    void createLinks(
        const std::map<SegmentPair, Transitions>& transitionMap,
        uint64_t minCoverage);

    // The transitions for each link.
    // Indexed by linkId.
    MemoryMapped::VectorOfVectors< pair<OrientedReadId, Transition>, uint64_t> transitions;
    uint64_t linkCoverage(uint64_t linkId) const
    {
        return transitions.size(linkId);
    }

    // The links for each source or target segments.
    // Indexed by segment id.
    MemoryMapped::VectorOfVectors<uint64_t, uint64_t> linksBySource;
    MemoryMapped::VectorOfVectors<uint64_t, uint64_t> linksByTarget;
    void createConnectivity();


    // Flag back-segments.
    // This does not do a full blown search for locally strongly connected components.
    // A segment is marked as a back-segment if:
    // - It has only a single incoming link.
    // - It has a single outgoing link.
    // - The incoming and outgoing links both connect to/from the same segment.
    void flagBackSegments();
    MemoryMapped::Vector<bool> isBackSegment;



    // Get the children or parents of a given segment.
    // Only use links with at least a specified coverage.
    void getChildren(
        uint64_t segmentId,
        uint64_t minimumLinkCoverage,
        vector<uint64_t>&
        ) const;
    void getParents(
        uint64_t segmentId,
        uint64_t minimumLinkCoverage,
        vector<uint64_t>&
        ) const;
    void getChildrenOrParents(
        uint64_t segmentId,
        uint64_t direction, // 0=forward (children), 1=backward (parents).
        uint64_t minimumLinkCoverage,
        vector<uint64_t>&
        ) const;


    // Find descendants of a given segment, up to a given distance in the graph.
    void findDescendants(
        uint64_t segmentId,
        uint64_t maxDistance,
        vector<uint64_t>& segmentIds
        ) const;

    void writeGfa(const string& fileName) const;
    void writeGfa(ostream&) const;

    // Find the distinct oriented reads that appear on the path
    // of a segment. Also return the average edge coverage for the path.
    double findOrientedReadsOnSegment(
        uint64_t segmentId,
        vector<OrientedReadId>&) const;



    // Get information about the oriented reads that appear on the
    // marker graph path of a segment.
    class SegmentOrientedReadInformation {
    public:

        // The oriented reads on this segment,
        // each storage with an average offset relative to the segment.
        class Info {
        public:
            OrientedReadId orientedReadId;

            // The average offset, in markers, between the
            // beginning of this oriented read and the
            // beginning of the segment.
            int32_t averageOffset;
        };
        vector<Info> infos;
    };
    void getOrientedReadsOnSegment(
        uint64_t segmentId,
        SegmentOrientedReadInformation&) const;

    // Oriented read information for each segment.
    // This is only stored when needed.
    vector<SegmentOrientedReadInformation> segmentOrientedReadInformation;
    void storeSegmentOrientedReadInformation(size_t threadCount);
    void storeSegmentOrientedReadInformationThreadFunction(size_t threadId);



    // Estimate the offset between two segments.
    // Takes as input SegmentOrientedReadInformation objects
    // for the two segments.
    // Common oriented reads between the two segments are used
    // to estimate the average offset, in markers,
    // between the beginning of the segments.
    // The number of common oriented reads
    // is computed and stored in the last argument.
    // If that is zero, the computed offset is not valid.
    void estimateOffset(
        const SegmentOrientedReadInformation& info0,
        const SegmentOrientedReadInformation& info1,
        int64_t& offset,
        uint64_t& commonOrientedReadCount
        ) const;



    // Analyze a pair of segments for common oriented reads,
    // offsets, missing reads, etc.
    class SegmentPairInformation {
    public:

        // The total number of oriented reads present in each segment.
        array<uint64_t, 2> totalCount = {0, 0};

        // The number of oriented reads present in both segments.
        // If this is zero, the rest of the information is not valid.
        uint64_t commonCount = 0;

        // The offset of segment 1 relative to segment 0, in markers.
        int64_t offset = std::numeric_limits<int64_t>::max();


        // The number of oriented reads present in each segment
        // but missing from the other segment,
        // and which should have been present based on the above estimated offset.
        array<uint64_t, 2> unexplainedCount = {0, 0};

        // The number of oriented reads that appear in only one
        // of the two segments, but based on the estimated offset
        // are too short to appear in the other segment.
        array<uint64_t, 2> shortCount = {0, 0};

        // Check that the above counts are consistent.
        void check() const
        {
            for(uint64_t i=0; i<2; i++) {
                SHASTA_ASSERT(commonCount + unexplainedCount[i] + shortCount[i] ==
                    totalCount[i]);
            }
        }

        // This computes the fraction of unexplained oriented reads,
        // without counting the short ones.
        double unexplainedFraction(uint64_t i) const
        {
            // return double(unexplainedCount[i]) / double(totalCount[i]);
            return double(unexplainedCount[i]) / double(commonCount + unexplainedCount[i]);
        }
        double maximumUnexplainedFraction() const
        {
            return max(unexplainedFraction(0), unexplainedFraction(1));
        }

        // Jaccard similarity, without counting the short reads.
        double jaccard() const
        {
            return double(commonCount) / double(commonCount + unexplainedCount[0] + unexplainedCount[1]);
        }
    };
    void analyzeSegmentPair(
        uint64_t segmentId0,
        uint64_t segmentId1,
        const SegmentOrientedReadInformation& info0,
        const SegmentOrientedReadInformation& info1,
        const MemoryMapped::VectorOfVectors<CompressedMarker, uint64_t>& markers,
        SegmentPairInformation&
        ) const;



    // Find segment pairs a sufficient number of common reads
    // and with low unexplained fraction (in both directions)
    // between segmentId0 and one of its descendants within the specified distance.
    // This requires the vector segmentOrientedReadInformation above to be
    // available.
    void findSegmentPairs(
        uint64_t segmentId0,
        uint64_t maxDistance,
        uint64_t minCommonReadCount,
        double maxUnexplainedFraction,
        vector<uint64_t>& segmentIds1
    ) const;



    // Cluster the segments based on read composition.
    // We find segment pairs a sufficient number of common reads
    // and with low unexplained fraction (in both directions).
    void clusterSegments(size_t threadCount, uint64_t minClusterSize);
    class ClusterSegmentsData {
    public:

        // The segment pairs found by each thread.
        // In each pair, the lower number segment comes first.
        vector< vector< pair<uint64_t, uint64_t> > > threadPairs;
    };
    ClusterSegmentsData clusterSegmentsData;
    void clusterSegmentsThreadFunction1(size_t threadId);
    void addClusterPairs(size_t threadId, uint64_t segmentId0);
    MemoryMapped::Vector<uint64_t> clusterIds;



    // Analyze a subgraph of the assembly graph.

    // Classes used in analyzeSubgraph.
    class AnalyzeSubgraphClasses {
    public:

        // A CompressedPseudoPathSnippet describes a sequence of consecutive positions
        // of the compressed pseudopath of an oriented read.
        // An OrientedReadId can have than more one snippet on a given subgraph,
        // but this is not common. It can happen if the assembly graph contains a cycle.
        class CompressedPseudoPathSnippet {
        public:

            // The OrientedReadId this refers to.
            OrientedReadId orientedReadId;

            // The sequence of segments encountered.
            vector<uint64_t> segmentIds;

            // The first and last position in the compressed pseudopath for this OrientedReadId.
            uint64_t firstPosition;
            uint64_t lastPosition() const
            {
                return firstPosition + segmentIds.size() - 1;
            }
        };

        // A Cluster is a set of CompressedPseudoPathSnippet's.
        class Cluster {
        public:
            // The snippet groups in this cluster.
            vector<uint64_t> snippetGroupIndexes;

            // The snippets in this cluster.
            vector<CompressedPseudoPathSnippet> snippets;
            uint64_t coverage() const
            {
                return snippets.size();
            }

            // The segments visited by the snippets of this cluster,
            // each stored with its coverage (number of snippets);
            vector< pair<uint64_t, uint64_t > > segments;
            vector<uint64_t> getSegments() const;

            // Remove segments with coverage less than the specified value.
            void cleanupSegments(uint64_t minClusterCoverage);

            // Construct the segments given the snippets.
            void constructSegments();
        };



        // The SnippetGraph is used by analyzeSubgraph2.
        // A vertex represents a set of snippets and stores
        // the corresponding snippet indexes.
        // An edge x->y is created if there is at least one snippet in y
        // that is an approximate subset of a snippet in x.
        // Strongly connected components are condensed, so after that
        //the graph is guaranteed to have no cycles.
        class SnippetGraphVertex {
        public:
            vector<uint64_t> snippetIndexes;
            uint64_t clusterId = std::numeric_limits<uint64_t>::max();
            SnippetGraphVertex() {}
            SnippetGraphVertex(uint64_t snippetIndex) :
                snippetIndexes(1, snippetIndex) {}
        };
        using SnippetGraphBaseClass =
            boost::adjacency_list<boost::setS, boost::listS, boost::bidirectionalS, SnippetGraphVertex>;
        class SnippetGraph : public SnippetGraphBaseClass {
        public:
            uint64_t clusterCount = 0;
            void findDescendants(const vertex_descriptor, vector<vertex_descriptor>&) const;
            void writeGraphviz(const string& fileName) const;
        };
    };



    void analyzeSubgraph1(
        const vector<uint64_t>& segmentIds,
        vector<AnalyzeSubgraphClasses::Cluster>&,
        bool debug) const;
    void analyzeSubgraph2(
        const vector<uint64_t>& segmentIds,
        vector<AnalyzeSubgraphClasses::Cluster>&,
        bool debug) const;
    template<uint64_t N> void analyzeSubgraph2Template(
        const vector<uint64_t>& segmentIds,
        vector<AnalyzeSubgraphClasses::Cluster>&,
        bool debug) const;



    // Create an assembly path starting at a given segment.
    void createAssemblyPath(
        uint64_t segmentId,
        uint64_t direction,    // 0 = forward, 1 = backward
        vector<uint64_t>& path // The segmentId's of the path.
        ) const;



    // Compute link separation given a set of Transitions.
    template<class Container> static double linkSeparation(
        const Container& transitions,
        uint64_t pathLength0)
    {
        double averageLinkSeparation = 0.;

        for(const pair<OrientedReadId, Transition>& p: transitions) {
            const Transition& transition = p.second;
            const auto& pseudoPathEntry0 = transition[0];
            const auto& pseudoPathEntry1 = transition[1];

            SHASTA_ASSERT(pseudoPathEntry1.ordinals[0] >= pseudoPathEntry0.ordinals[1]);

            const int64_t linkSeparation =
                int64_t(pseudoPathEntry1.ordinals[0] - pseudoPathEntry0.ordinals[1]) -
                int64_t(pathLength0 - 1 - pseudoPathEntry0.position) -
                int64_t(pseudoPathEntry1.position);
            averageLinkSeparation += double(linkSeparation);
        }
        averageLinkSeparation /= double(transitions.size());

        return averageLinkSeparation;
    }
};




#endif

