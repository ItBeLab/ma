/**
 * @file sweepSvJumps.h
 * @author Markus Schmidt
 */
#pragma once

#include "container/squeezedVector.h"
#include "container/sv_db/query_objects/callInserter.h" // NEW DB API implemented
#include "container/sv_db/query_objects/fetchSvJump.h"
#include "container/sv_db/svSchema.h"
#include "module/module.h"
#include "util/statisticSequenceAnalysis.h"
#include <cmath>
#include <csignal>

#define ADDITIONAL_DEBUG 0

namespace libMA
{

class GenomeSection : public Container, public geomUtil::Interval<int64_t>
{
    using Interval<int64_t>::Interval;
}; // class

/**
 * @brief generates evenly spaced intervals over the length of the given pack
 * @details used for parallel implementation of the complete bipartite subgraph (CBSG) sweep.
 * last segment will most likely extend over the end of the genome.
 */
class GenomeSectionFactory : public Module<GenomeSection, true>
{
  public:
    int64_t iRefSize;
    int64_t iSectionSize;
    int64_t iCurrStart;
    /**
     * @brief
     * @details
     */
    GenomeSectionFactory( const ParameterSetManager& rParameters, std::shared_ptr<Pack> pPack )
        : iRefSize( (int64_t)pPack->uiStartOfReverseStrand( ) ),
          // compute the number of genome sections so that there are 100 sections for each thread
          // 50 * 2 because forw & rev. further the sections should all be at least 10000nt long
          // otherwise we do so much extra work with re overlapping parts between sections,
          // that parallel execution is not worth it.
          iSectionSize( std::max( iRefSize / ( int64_t )( rParameters.getNumThreads( ) * 50 ), (int64_t)500000 ) ),
          iCurrStart( 0 )
    {} // constructor

    virtual std::shared_ptr<GenomeSection> EXPORTED execute( )
    {
        // setFinished( );
        // return std::make_shared<GenomeSection>( 0, std::numeric_limits<int64_t>::max( ) - 10000 );

        std::shared_ptr<GenomeSection> pRet;
        if( iCurrStart % 2 == 0 ) // forward strand
            pRet = std::make_shared<GenomeSection>( ( iCurrStart / 2 ) * iSectionSize, iSectionSize );
        else // reverse strand
            pRet = std::make_shared<GenomeSection>(
                ( iCurrStart / 2 ) * iSectionSize + std::numeric_limits<int64_t>::max( ) / (int64_t)2, iSectionSize );

        iCurrStart++;
        if( ( iCurrStart / 2 ) * iSectionSize >= iRefSize )
            setFinished( );
        return pRet;
    } // method

    virtual bool requiresLock( ) const
    {
        return true;
    } // method
}; // class


class CompleteBipartiteSubgraphClusterVector : public Container
{
  public:
    std::vector<std::shared_ptr<SvCall>> vContent;
}; // class

/**
 * @brief saves all computed clusters in the database
 * @details
 */
template <typename DBCon> class SvCallSink : public Module<Container, false, CompleteBipartiteSubgraphClusterVector>
{
  public:
    std::shared_ptr<SV_Schema<DBCon>> pDB;
    int64_t iRunId;
    /**
     * @brief
     * @details
     */
    SvCallSink( const ParameterSetManager& rParameters, std::shared_ptr<SV_Schema<DBCon>> pDB,
                std::string rsSvCallerName, std::string rsSvCallerDesc, int64_t uiJumpRunId )
        : pDB( pDB ), iRunId( pDB->pSvCallerRunTable->insert_( rsSvCallerName, rsSvCallerDesc, uiJumpRunId ) )
    {} // constructor

    virtual std::shared_ptr<Container> EXPORTED execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pVec )
    {
        {
            std::lock_guard<std::mutex> xGuard( *pDB->pWriteLock );
            auto pInserter = std::make_shared<SvCallInserter<DBCon>>( pDB, iRunId );
            for( auto pCall : pVec->vContent )
                pInserter->insertCall( *pCall );
        } // scope for pInserter (transaction)
        return std::make_shared<Container>( );
    } // method & scope for xGuard
}; // class

/**
 * @brief saves all computed clusters in the database
 * @details buffers the calls in a vector before
 * @note in a parallel computational graph: use multiple instances of this module
 */
template <typename DBCon>
class BufferedSvCallSink : public Module<Container, false, CompleteBipartiteSubgraphClusterVector>
{
  public:
    std::shared_ptr<SvCallInserter<DBCon>> pInserter;
    std::vector<std::shared_ptr<CompleteBipartiteSubgraphClusterVector>> vContent;
    size_t uiEleCnt = 0; // element count
    /**
     * @brief
     * @details
     */
    BufferedSvCallSink( const ParameterSetManager& rParameters, std::shared_ptr<SvCallInserter<DBCon>> pInserter )
        : pInserter( pInserter )
    {} // constructor

    inline void commit( bool bForce = false )
    {
        if( vContent.size( ) == 0 )
            return;
        if( !bForce && uiEleCnt < 10000 )
            return;

        {
            std::lock_guard<std::mutex> xGuard( *pInserter->pDB->pWriteLock );
            for( auto pVec : vContent )
                for( auto pCall : pVec->vContent )
                    pInserter->insertCall( *pCall );
            pInserter->reOpenTransaction( );
        } // scope for xGuard

        vContent.clear( );
        uiEleCnt = 0;
    } // method

    ~BufferedSvCallSink( )
    {
        commit( true );
    } // scope for pInserter (transaction)

    virtual std::shared_ptr<Container> EXPORTED execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pVec )
    {
        vContent.push_back( pVec );
        uiEleCnt += pVec->vContent.size( );
        commit( );
        return std::make_shared<Container>( );
    } // method & scope for xGuard
}; // class

/**
 * @brief
 * @details
 */
template <typename DBCon>
class CompleteBipartiteSubgraphSweep : public Module<CompleteBipartiteSubgraphClusterVector, false, GenomeSection>
{
  public:
    const ParameterSetManager& rParameters;
    std::shared_ptr<SV_Schema<DBCon>> pSvDb;
    std::shared_ptr<Pack> pPack;
    int64_t iSvCallerRunId;
    int64_t iMaxFuzziness;
    nucSeqIndex uiGenomeSize;
    size_t uiSqueezeFactor;
    size_t uiCenterStripUp;
    size_t uiCenterStripDown;

    // record the times each step takes
    double dInit = 0;
    double dOuterWhile = 0;
    double dInnerWhile = 0;

    /**
     * @brief
     * @details
     */
    CompleteBipartiteSubgraphSweep( const ParameterSetManager& rParameters, std::shared_ptr<SV_Schema<DBCon>> pSvDb,
                                    std::shared_ptr<Pack> pPack, int64_t iSvCallerRunId, int64_t iSequencerId )
        : rParameters( rParameters ),
          pSvDb( std::make_shared<SV_Schema<DBCon>>( *pSvDb ) ),
          pPack( pPack ),
          iSvCallerRunId( iSvCallerRunId ),
          // @todo this does not consider tail edges (those should be limited in size and then here we should use the
          // max of both limits)
          // also this should be the maximal cluster width not the maximal CBSG width
          iMaxFuzziness( (int64_t)rParameters.getSelected( )->xJumpH->get( ) * 10 ),
          uiGenomeSize( pPack->uiStartOfReverseStrand( ) ),
          uiSqueezeFactor( 5000 ),
          uiCenterStripUp( 5000 ),
          uiCenterStripDown( 1000 )
    {} // constructor

    virtual std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
        EXPORTED execute( std::shared_ptr<GenomeSection> pSection )
    {
        auto xInitStart = std::chrono::high_resolution_clock::now( );
        // std::cout << "SortedSvJumpFromSql (" << pSection->iStart << ")" << std::endl;
        SortedSvJumpFromSql<DBCon> xEdges(
            rParameters, pSvDb, iSvCallerRunId,
            // make sure we overlap the start of the next interval, so that clusters that span over two intervals
            // are being collected. -> for this we just keep going after the end of the interval
            pSection->start( ) > iMaxFuzziness ? pSection->start( ) - iMaxFuzziness : 0,
            pSection->end( ) + iMaxFuzziness );

        // std::cout << "sweep (" << pSection->iStart << ")" << std::endl;
        nucSeqIndex uiForwStrandStart = (nucSeqIndex)pSection->start( );
        nucSeqIndex uiForwStrandEnd = (nucSeqIndex)pSection->end( );
        if( pSection->iStart >= std::numeric_limits<int64_t>::max( ) / (int64_t)2 )
        {
            uiForwStrandStart =
                ( nucSeqIndex )( pSection->start( ) - std::numeric_limits<int64_t>::max( ) / (int64_t)2 );
            uiForwStrandEnd = ( nucSeqIndex )( pSection->end( ) - std::numeric_limits<int64_t>::max( ) / (int64_t)2 );
        } // if

        SqueezedVector<std::shared_ptr<SvCall>> xPointerVec( uiGenomeSize, uiSqueezeFactor, uiCenterStripUp,
                                                             uiCenterStripDown );

        auto pRet = std::make_shared<CompleteBipartiteSubgraphClusterVector>( );
        // return pRet;

#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
        std::set<int64_t> xVisitedStart;
        std::vector<std::shared_ptr<SvCall>> xActiveClusters;
#endif
        auto xInitEnd = std::chrono::high_resolution_clock::now( );
        std::chrono::duration<double> xDiffInit = xInitEnd - xInitStart;
        dInit += xDiffInit.count( );

        auto xLoopStart = std::chrono::high_resolution_clock::now( );
        while( xEdges.hasNextStart( ) || xEdges.hasNextEnd( ) )
        {
            if( xEdges.nextStartIsSmaller( ) )
            {
                auto pEdge = xEdges.getNextStart( );
                // edge actually outside of considered area
                if( pEdge->from_end( ) > pSection->end( ) + iMaxFuzziness )
                    continue;
                auto xInnerStart = std::chrono::high_resolution_clock::now( );
#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
                xVisitedStart.insert( pEdge->iId );
#endif
                auto pNewCluster = std::make_shared<SvCall>( pEdge );

                size_t uiStart =
                    xPointerVec.to_physical_coord( pNewCluster->xXAxis.end( ), pNewCluster->xYAxis.start( ) );
                size_t uiEnd =
                    xPointerVec.to_physical_coord( pNewCluster->xXAxis.start( ), pNewCluster->xYAxis.end( ) );
                assert( uiEnd >= uiStart );
                // set the clusters y coodinate to the physical coords (we won't use the actual coords anyways)
                // this is necessary, since we need to work with these coords when joining clusters
                pNewCluster->xYAxis.start( uiStart );
                pNewCluster->xYAxis.size( uiEnd - uiStart );

                // join with all covered clusters; make sure that we don't join the same cluster twice
                std::shared_ptr<SvCall> pLastJoined;
                for( size_t uiI = uiStart; uiI <= uiEnd; uiI++ )
                    if( xPointerVec.get( )[ uiI ] != nullptr && pLastJoined != xPointerVec.get( )[ uiI ] )
                    {
                        pLastJoined = xPointerVec.get( )[ uiI ];
#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
                        assert( pLastJoined->uiOpenEdges > 0 );
                        xActiveClusters.erase( std::remove_if( xActiveClusters.begin( ), xActiveClusters.end( ),
                                                               [&]( auto pX ) { return pX == pLastJoined; } ),
                                               xActiveClusters.end( ) );
#endif
                        pNewCluster->join( *pLastJoined );
                    } // if

#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
                xActiveClusters.push_back( pNewCluster );
#endif
                // redirect all covered pointers to the new cluster
                for( size_t uiI = pNewCluster->xYAxis.start( ); uiI <= pNewCluster->xYAxis.end( ); uiI++ )
                {
#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
                    if( xPointerVec.get( )[ uiI ] != nullptr )
                        for( int64_t iId : xPointerVec.get( )[ uiI ]->vSupportingJumpIds )
                            assert( std::find( pNewCluster->vSupportingJumpIds.begin( ),
                                               pNewCluster->vSupportingJumpIds.end( ),
                                               iId ) != pNewCluster->vSupportingJumpIds.end( ) );
#endif
                    xPointerVec.get( )[ uiI ] = pNewCluster;
                } // for
                auto xInnerEnd = std::chrono::high_resolution_clock::now( );
                std::chrono::duration<double> xDiffInner = xInnerEnd - xInnerStart;
                dInnerWhile += xDiffInner.count( );
            } // if
            else
            {
                auto pEndJump = xEdges.getNextEnd( );
                // edge actually outside of considered area
                if( pEndJump->from_start( ) + iMaxFuzziness < pSection->start( ) )
                    continue;
                auto xInnerStart = std::chrono::high_resolution_clock::now( );
#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
                assert( xVisitedStart.count( pEndJump->iId ) != 0 );
#endif
                // find the correct cluster for this edge
                auto pCluster = xPointerVec.get( )[ xPointerVec.to_physical_coord(
                    pEndJump->from_start_same_strand( ) + pEndJump->from_size( ), pEndJump->to_start( ) ) ];
                assert( pCluster != nullptr );
                assert( std::find( pCluster->vSupportingJumpIds.begin( ), pCluster->vSupportingJumpIds.end( ),
                                   pEndJump->iId ) != pCluster->vSupportingJumpIds.end( ) );
                pCluster->uiOpenEdges--;
                // check if we want to save the cluster
                if( pCluster->uiOpenEdges == 0 )
                {
                    for( size_t uiI = pCluster->xYAxis.start( ); uiI <= pCluster->xYAxis.end( ); uiI++ )
                        xPointerVec.get( )[ uiI ] = nullptr;

#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
                    xActiveClusters.erase( std::remove_if( xActiveClusters.begin( ), xActiveClusters.end( ),
                                                           [&]( auto pX ) { return pX == pCluster; } ),
                                           xActiveClusters.end( ) );
#endif
                    if( pCluster->xXAxis.start( ) < uiForwStrandEnd && pCluster->xXAxis.start( ) >= uiForwStrandStart )
                        pRet->vContent.push_back( pCluster );
                } // if
                auto xInnerEnd = std::chrono::high_resolution_clock::now( );
                std::chrono::duration<double> xDiffInner = xInnerEnd - xInnerStart;
                dInnerWhile += xDiffInner.count( );
            } // else
        } // while
        // std::cout << "done (" << pSection->iStart << ")" << std::endl;
        auto xLoopEnd = std::chrono::high_resolution_clock::now( );
        std::chrono::duration<double> xDiffLoop = xLoopEnd - xLoopStart;
        dOuterWhile += xDiffLoop.count( );

#if DEBUG_LEVEL > 0 && ADDITIONAL_DEBUG > 0
        // make sure that there is no open cluster left
        for( auto pCluster : xPointerVec.get( ) )
            assert( pCluster == nullptr );
#endif

        return pRet;
    } // method
}; // class

/**
 * @brief
 * @details
 */
template <typename DBCon>
class ExactCompleteBipartiteSubgraphSweep
    : public Module<CompleteBipartiteSubgraphClusterVector, false, CompleteBipartiteSubgraphClusterVector>
{
  public:
    std::shared_ptr<Pack> pPack;
    int64_t iMaxInsertRatioDiff = 150;
    /**
     * @brief
     * @details
     */
    ExactCompleteBipartiteSubgraphSweep( const ParameterSetManager& rParameters,
                                         std::shared_ptr<SV_Schema<DBCon>> pSvDb, std::shared_ptr<Pack> pPack,
                                         int64_t iSequencerId )
        : pPack( pPack )
    {} // constructor

    inline void exact_sweep( std::vector<std::shared_ptr<SvJump>>& rvEdges, size_t uiStart, size_t uiEnd,
                             std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pRet )
    {
        // squash sv_jump indices
        std::map<int64_t, size_t> xSquashedY;
        std::vector<std::shared_ptr<SvJump>> vEdgesStart;
        std::vector<std::shared_ptr<SvJump>> vEdgesEnd;
        while( uiStart < uiEnd )
        {
            std::shared_ptr<SvJump> pJmp = rvEdges[ uiStart ];
            xSquashedY[ pJmp->to_start( ) ] = 0;
            xSquashedY[ pJmp->sweep_end( ) + 1 ] = 0;
            vEdgesStart.push_back( pJmp );
            vEdgesEnd.push_back( pJmp );
            uiStart++;
        } // for

        // set the indices in the squashed map correctly
        {
            size_t uiI = 0;
            for( auto xIter = xSquashedY.begin( ); xIter != xSquashedY.end( ); ++xIter )
            {
                xIter->second = uiI;
                uiI++;
            } // for
        } // scope

        // create start list
        std::sort( vEdgesStart.begin( ), vEdgesStart.end( ),
                   []( std::shared_ptr<SvJump> pA, std::shared_ptr<SvJump> pB ) {
                       return pA->from_start( ) < pB->from_start( );
                   } ); // sort function call

        // create end list
        std::sort( vEdgesEnd.begin( ), vEdgesEnd.end( ), []( std::shared_ptr<SvJump> pA, std::shared_ptr<SvJump> pB ) {
            return pA->from_end( ) < pB->from_end( );
        } ); // sort function call

        // initialize the y-axis sweep pointer and counter vector
        std::vector<std::pair<std::shared_ptr<SvCall>, size_t>> vSweepVec( xSquashedY.size( ) );

        // do the actual sweep
        size_t uiI = 0;
        size_t uiJ = 0;
        while( uiJ < vEdgesEnd.size( ) )
        {
            if( uiI < vEdgesStart.size( ) && vEdgesStart[ uiI ]->from_start( ) <= vEdgesEnd[ uiJ ]->from_end( ) )
            {
                // create a cluster containing merely the current call
                auto pNewCluster = std::make_shared<SvCall>( vEdgesStart[ uiI ] );

                // turn tail edge lines into squares
                if( !vEdgesStart[ uiI ]->switch_strand_known( ) )
                    pNewCluster->xYAxis.size( vEdgesStart[ uiI ]->from_size( ) );

                // join with all overlapping clusters
                size_t uiStart = xSquashedY[ vEdgesStart[ uiI ]->to_start( ) ];
                size_t uiIdx = uiStart;
                size_t uiEnd = xSquashedY[ vEdgesStart[ uiI ]->sweep_end( ) + 1 ];
                std::set<std::shared_ptr<SvCall>> xJoinedClusters;
                while( uiIdx <= uiEnd )
                {
                    if( vSweepVec[ uiIdx ].second > 0 &&
                        xJoinedClusters.find( vSweepVec[ uiIdx ].first ) == xJoinedClusters.end( ) )
                    {
                        pNewCluster->join( *vSweepVec[ uiIdx ].first );
                        xJoinedClusters.insert( vSweepVec[ uiIdx ].first );
                    } // if
                    uiIdx++;
                } // while

                // insert the newly computed cluster into the pointer vector and counter vector
                uiIdx = xSquashedY[ pNewCluster->xYAxis.start( ) ];
                size_t uiEnd2 = xSquashedY[ pNewCluster->xYAxis.end( ) + 1 ];
                while( uiIdx <= uiEnd2 )
                {
                    if( uiStart <= uiIdx && uiIdx <= uiEnd )
                    {
                        vSweepVec[ uiIdx ].second++;
                        vSweepVec[ uiIdx ].first = pNewCluster;
                    } // if
                    else if( vSweepVec[ uiIdx ].second > 0 &&
                             xJoinedClusters.find( vSweepVec[ uiIdx ].first ) != xJoinedClusters.end( ) )
                        vSweepVec[ uiIdx ].first = pNewCluster;
                    uiIdx++;
                } // while

                uiI++;
            } // if
            else
            {
                size_t uiStart = xSquashedY[ vEdgesEnd[ uiJ ]->to_start( ) ];
                size_t uiEnd = xSquashedY[ vEdgesEnd[ uiJ ]->sweep_end( ) + 1 ];
                auto pCurrCluster = vSweepVec[ uiStart ].first;
                assert( pCurrCluster != nullptr );
                pCurrCluster->uiOpenEdges -= 1;
                // check if that closes the cluster
                if( pCurrCluster->uiOpenEdges == 0 )
                {
#if 1
                    // remove jumps with equal read id's
                    std::set<std::shared_ptr<SvJump>,
                             std::function<bool( std::shared_ptr<SvJump>, std::shared_ptr<SvJump> )>>
                    xNewJumps( []( std::shared_ptr<SvJump> pA, std::shared_ptr<SvJump> pB ) -> bool {
                        return pA->iReadId < pB->iReadId;
                    } ); // std::set constructor call
                    std::sort( pCurrCluster->vSupportingJumps.begin( ), pCurrCluster->vSupportingJumps.end( ),
                               []( std::shared_ptr<SvJump> pA, std::shared_ptr<SvJump> pB ) {
                                   return pA->query_distance( ) < pB->query_distance( );
                               } );
                    for( auto pJump : pCurrCluster->vSupportingJumps )
                        xNewJumps.insert( pJump );
                    pCurrCluster->vSupportingJumps.clear( );
                    pCurrCluster->vSupportingJumpIds.clear( );
                    for( auto& xTup : xNewJumps )
                    {
                        pCurrCluster->vSupportingJumps.push_back( xTup );
                        pCurrCluster->vSupportingJumpIds.push_back( xTup->iId );
                    } // for
#endif

                    pCurrCluster->reEstimateClusterSize( );
                    // we messed up this counter by removing jumps, fix that
                    pCurrCluster->uiNumSuppReads = pCurrCluster->vSupportingJumps.size( );
                    // save the cluster
                    pRet->vContent.push_back( pCurrCluster );
                } // if
                // decrement the counter vector
                while( uiStart <= uiEnd )
                    vSweepVec[ uiStart++ ].second--;
                uiJ++;
            } // else
        } // while


    } // method

    /// complete linkage clustering for jump distances
    /// we call sweep_sv_jumps for all insert_ratio clusters with a max dist of max_insert_ratio_diff
    /// this clustering is necessary because there might be an edge in the graph that has several different inserted
    /// sequences. We need to consider these sequences individually -> cluster by sequence length
    /// if the sequences are different by nucleotides, we need to figure that out later in the multialignment step...
    inline void lineSweep( std::shared_ptr<SvCall> pCluster, //
                           std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
                               pRet )
    {
        std::vector<std::shared_ptr<SvJump>>& rvEdges = pCluster->vSupportingJumps;
        std::sort( rvEdges.begin( ), rvEdges.end( ), []( std::shared_ptr<SvJump> pA, std::shared_ptr<SvJump> pB ) {
            if( pA->insert_ratio( ) != pB->insert_ratio( ) )
                return pA->insert_ratio( ) < pB->insert_ratio( );
            return pA->query_distance( ) < pB->query_distance( );
        } ); // sort function call

        // i & j are the start and end positions of the clusters, respectiveley
        // this works by setting i to the start of the cluster and then gradually increasing j while it belongs to the
        // complete linkage cluster (we work on sorted linear data)
        size_t uiI = 0;
        size_t uiJ = 0;

        while( uiI < rvEdges.size( ) )
        {
            // increase j if the insert_ratio between the 'i' and 'j' object is closer than 'max_insert_ratio_diff'
            // if we reach a tail edge (those edges are sorted to the end since their insert
            // size is 'inf') we check if the current insert ratio is larger than the tail of the read that created the
            // edge if so we join the tail edge into the cluster.
            if( uiJ < rvEdges.size( ) &&
                rvEdges[ uiI ]->insert_ratio( ) >= ( rvEdges[ uiJ ]->switch_strand_known( )
                                                         ? rvEdges[ uiJ ]->insert_ratio( ) - iMaxInsertRatioDiff
                                                         : (int64_t)rvEdges[ uiJ ]->query_distance( ) ) )
                uiJ++;
            else
            {
                this->exact_sweep( rvEdges, uiI, uiJ, pRet );
                uiI = uiJ;
            } // else
        } // while
    } // method

    virtual std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
        EXPORTED execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pClusters )
    {
        auto pRet = std::make_shared<CompleteBipartiteSubgraphClusterVector>( );

        for( auto pCluster : pClusters->vContent )
            this->lineSweep( pCluster, pRet );

        return pRet;
    } // method
}; // class

#define ANALYZE_FILTERS ( 1 )

class AbstractFilter
{
  public:
#if ANALYZE_FILTERS
    std::string sName;
    size_t uiFilterKept = 0;
    size_t uiFilterTotal = 0;
    std::mutex xLock;
#endif
    AbstractFilter( std::string sName )
#if ANALYZE_FILTERS
        : sName( sName )
#endif
    {} // constructor
#if ANALYZE_FILTERS

    ~AbstractFilter( )
    {
        if( uiFilterTotal > 0 )
            std::cout << "~" << sName << ": filter kept and eliminated " << uiFilterKept << " and "
                      << uiFilterTotal - uiFilterKept << " elements respectiveley.\n\tThat's "
                      << ( ( 1000 * uiFilterKept ) / uiFilterTotal ) / 10.0 << "% and "
                      << 100.0 - ( ( 1000 * uiFilterKept ) / uiFilterTotal ) / 10.0 << "% respectiveley." << std::endl;
    } // deconstructor
#endif
}; // class

/**
 * @brief filters out short calls with low support
 * @details
 * Due to the high concentration of noise along the diagonal of the adjacency matrix we get a lot of false positives
 * here. This module filters such calls based on the amount of Nt's that support the individual calls.
 */
class FilterLowSupportShortCalls
    : public Module<CompleteBipartiteSubgraphClusterVector, false, CompleteBipartiteSubgraphClusterVector>,
      public AbstractFilter
{
  public:
    nucSeqIndex uiMaxSuppNt;
    nucSeqIndex uiMaxSVSize;

    FilterLowSupportShortCalls( const ParameterSetManager& rParameters )
        : AbstractFilter( "FilterLowSupportShortCalls" ),
          uiMaxSuppNt( rParameters.getSelected( )->xMaxSuppNtShortCallFilter->get( ) ),
          uiMaxSVSize( rParameters.getSelected( )->xMaxCallSizeShortCallFilter->get( ) )
    {} // constructor

    std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
    execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pCalls )
    {
        auto pRet = std::make_shared<CompleteBipartiteSubgraphClusterVector>( );
        for( auto pCall : pCalls->vContent )
            // if the call is supported by enough NT's or large enough we keep it
            if( pCall->getScore( ) > uiMaxSuppNt || pCall->size( ) > uiMaxSVSize )
                pRet->vContent.push_back( pCall );
#if ANALYZE_FILTERS
        std::lock_guard<std::mutex> xGuard( xLock );
        uiFilterTotal += pCalls->vContent.size( );
        uiFilterKept += pRet->vContent.size( );
#endif
        return pRet;
    } // method

}; // class

/**
 * @brief filters out fuzzy calls
 * @details
 * Observation: the seed pair clusters resulting false positive calls are generally way more spread out with
 * respect to the seed positions on the reference. This causes the statistical cluster size estimation to be very
 * conservative and output a very large cluster. We can use this behaviour to implement a simple filter that eliminates
 * a bunch of false positives.
 */
class FilterFuzzyCalls
    : public Module<CompleteBipartiteSubgraphClusterVector, false, CompleteBipartiteSubgraphClusterVector>,
      public AbstractFilter
{
  public:
    nucSeqIndex uiMaxFuzziness;
    FilterFuzzyCalls( const ParameterSetManager& rParameters )
        : AbstractFilter( "FilterFuzzyCalls" ),
          uiMaxFuzziness( rParameters.getSelected( )->xMaxFuzzinessFilter->get( ) )
    {} // constructor

    std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
    execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pCalls )
    {
        auto pRet = std::make_shared<CompleteBipartiteSubgraphClusterVector>( );
        for( auto pCall : pCalls->vContent )
            // if the call is presice enough we keep it
            if( pCall->xXAxis.size( ) <= uiMaxFuzziness && pCall->xYAxis.size( ) <= uiMaxFuzziness )
                pRet->vContent.push_back( pCall );
#if ANALYZE_FILTERS
        std::lock_guard<std::mutex> xGuard( xLock );
        uiFilterTotal += pCalls->vContent.size( );
        uiFilterKept += pRet->vContent.size( );
#endif
        return pRet;
    } // method
}; // class

/**
 * @brief filters out calls that are on a diagonal line
 * @details
 * Observation: some false positive calls result from jumps that lie on a 45 degree diagonal (bottom left to top right)
 * line. These calls create a small fuzziness so they are not detected by FilterFuzzyCalls.
 * Solution:
 * measure the standard deviation of the distance on both 45 degree diagonals (bottom left to top right &
 * bottom right to top left). If the bottom left to top right diagonal shows a high distacne and the other one does
 * not filter out the call. Do this via the delta positions of jumps.
 * (see Thoughts 06.ppt type 1)
 */
class FilterDiagonalLineCalls
    : public Module<CompleteBipartiteSubgraphClusterVector, false, CompleteBipartiteSubgraphClusterVector>,
      public AbstractFilter
{
  public:
    int64_t iFilterDiagonalLineCalls;
    FilterDiagonalLineCalls( const ParameterSetManager& rParameters )
        : AbstractFilter( "FilterDiagonalLineCalls" ), iFilterDiagonalLineCalls( 300 )
    {} // constructor

    inline int64_t getStd( std::vector<int64_t>& vX )
    {
        std::sort( vX.begin( ), vX.end( ) );

        int64_t iMean;
        if( vX.size( ) % 2 == 1 )
            iMean = vX[ vX.size( ) / 2 ];
        else
            iMean = ( vX[ vX.size( ) / 2 - 1 ] + vX[ vX.size( ) / 2 ] ) / 2;
        int64_t iSquaredDiff = 0;
        for( int64_t iI : vX )
            iSquaredDiff += ( iMean - iI ) * ( iMean - iI );
        return iSquaredDiff / vX.size( );
    } // method

    std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
    execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pCalls )
    {
        auto pRet = std::make_shared<CompleteBipartiteSubgraphClusterVector>( );
        for( auto pCall : pCalls->vContent )
        {
            std::vector<int64_t> vDiagonalA, vDiagonalB;
            for( auto pJump : pCall->vSupportingJumps )
            {
                int64_t iX = pJump->uiFrom;
                int64_t iY = pJump->uiTo;
                vDiagonalA.push_back( iY - iX );
                vDiagonalB.push_back( iY + iX );
            } // for
            int64_t iStdA, iStdB;
            iStdA = getStd( vDiagonalA );
            iStdB = std::max( getStd( vDiagonalB ), (int64_t)1 );
            if( iStdA / iStdB < iFilterDiagonalLineCalls )
                pRet->vContent.push_back( pCall );
        } // for
#if ANALYZE_FILTERS
        std::lock_guard<std::mutex> xGuard( xLock );
        uiFilterTotal += pCalls->vContent.size( );
        uiFilterKept += pRet->vContent.size( );
#endif
        return pRet;
    } // method
}; // class


/**
 * @brief compute the ambiguity of a call via sampling
 * @details
 * This samples how much over the statistical value the k-mer size needs to be, so that all k-mers around the call
 * are unique. \n
 * We consider 4 different sections on the reference:
 *  - To the 'left' of the 'from' coordinate of the call (on the 2d plane: left)
 *  - To the 'right' of the 'from' coordinate of the call (on the 2d plane: right)
 *  - To the 'left' of the 'to' coordinate of the call (on the 2d plane: bottom)
 *  - To the 'right' of the 'to' coordinate of the call (on the 2d plane: top)
 * We pick the maximum of two pairs, where the pairing is decided by wether or not the call switches strand:
 *  - if we switch strands we have to combine one 'left' with one 'right'
 *  - if we don't switch strand we have to match the two 'left's and two 'right's
 *  - we always have to pick one 'from' and one 'to' together.
 */
class ComputeCallAmbiguity
    : public Module<CompleteBipartiteSubgraphClusterVector, false, CompleteBipartiteSubgraphClusterVector, Pack>
{
    nucSeqIndex uiDistance;

  public:
    ComputeCallAmbiguity( const ParameterSetManager& rParameters )
        : uiDistance( rParameters.getSelected( )->xMaxCallSizeShortCallFilter->get( ) )
    {} // constructor

    std::shared_ptr<NucSeq> getRegion( nucSeqIndex uiPos, bool bLeftDirection, std::shared_ptr<Pack> pPack )
    {
        // due to their fuzziness calls can reach past the end of the genome
        if( uiPos >= pPack->uiUnpackedSizeForwardStrand )
            uiPos = pPack->uiUnpackedSizeForwardStrand - 1;

        auto uiSeqId = pPack->uiSequenceIdForPosition( uiPos );
        if( bLeftDirection )
        {
            nucSeqIndex iStartOfContig = pPack->startOfSequenceWithId( uiSeqId );
            nucSeqIndex uiStart = uiPos > iStartOfContig + uiDistance ? uiPos - uiDistance : iStartOfContig;
            nucSeqIndex uiSize = uiPos - uiStart;
            // return empty sequence for size = 0 cause pack throws exception otherwise
            if( uiSize == 0 )
                return std::make_shared<NucSeq>( );
            if( pPack->bridgingSubsection( uiStart, uiSize ) )
                pPack->unBridgeSubsection( uiStart, uiSize );
            return pPack->vExtract( uiStart, uiStart + uiSize );
        } // if
        else
        {
            nucSeqIndex iEndOfContig = pPack->endOfSequenceWithId( uiSeqId );
            nucSeqIndex uiEnd = uiPos + uiDistance < iEndOfContig ? uiPos + uiDistance : iEndOfContig;
            nucSeqIndex uiSize = uiEnd - uiPos;
            // return empty sequence for size = 0 cause pack throws exception otherwise
            if( uiSize == 0 )
                return std::make_shared<NucSeq>( );
            if( pPack->bridgingSubsection( uiPos, uiSize ) )
                pPack->unBridgeSubsection( uiPos, uiSize );
            return pPack->vExtract( uiPos, uiPos + uiSize );
        } // else
    } // method

    nucSeqIndex sampleAmbiguity( std::shared_ptr<NucSeq> pSeqA, std::shared_ptr<NucSeq> pSeqB )
    {
        // + 1 to avoind division by zero error
        return std::max( 1, (int)sampleSequenceAmbiguity( *pSeqA, *pSeqB, 0.001 ) - (int)pSeqA->length( ) -
                                (int)pSeqB->length( ) );
    } // method

    std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
    execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pCalls, std::shared_ptr<Pack> pPack )
    {
        for( auto pCall : pCalls->vContent )
        {
            auto f = pCall->xXAxis.start( ) + pCall->xXAxis.size( ) / 2;
            auto t = pCall->xYAxis.start( ) + pCall->xYAxis.size( ) / 2;
            // std::abs is ambigious under msvc...
            auto uiCallSize = f >= t ? f - t : t - f;

            if( uiCallSize > uiDistance )
            {
                auto pLeftFrom = getRegion( pCall->xXAxis.end( ), true, pPack );
                auto pRightFrom = getRegion( pCall->xXAxis.start( ), false, pPack );
                auto pLeftTo = getRegion( pCall->xYAxis.end( ), true, pPack );
                auto pRightTo = getRegion( pCall->xYAxis.start( ), false, pPack );

                // if we switch strand we have to compare forward and reverse strands
                if( pCall->bSwitchStrand )
                {
                    pLeftTo->vReverseAll( );
                    pLeftTo->vSwitchAllBasePairsToComplement( );
                    pRightTo->vReverseAll( );
                    pRightTo->vSwitchAllBasePairsToComplement( );
                } // if

                auto a = sampleAmbiguity( pLeftFrom, pCall->bSwitchStrand ? pRightTo : pLeftTo );
                auto b = sampleAmbiguity( pRightFrom, pCall->bSwitchStrand ? pLeftTo : pRightTo );

                pCall->uiReferenceAmbiguity = std::max( a, b );
            } // if
            else
                // @todo how to evaluate such calls?
                pCall->uiReferenceAmbiguity = 1;
        } // for
        return pCalls;
    } // method
}; // class

/**
 * @brief filters out short calls with low support
 * @details
 * Due to the high concentration of noise along the diagonal of the adjacency matrix we get a lot of false positives
 * here. This module filters such calls based on the amount of Nt's that support the individual calls.
 */
class FilterLowScoreCalls
    : public Module<CompleteBipartiteSubgraphClusterVector, false, CompleteBipartiteSubgraphClusterVector>,
      public AbstractFilter
{
  public:
    double dMinScore = 2.0;

    FilterLowScoreCalls( const ParameterSetManager& rParameters ) : AbstractFilter( "FilterLowScoreCalls" )
    {} // constructor

    std::shared_ptr<CompleteBipartiteSubgraphClusterVector>
    execute( std::shared_ptr<CompleteBipartiteSubgraphClusterVector> pCalls )
    {
        auto pRet = std::make_shared<CompleteBipartiteSubgraphClusterVector>( );
        for( auto pCall : pCalls->vContent )
            // if the call is supported by enough NT's or large enough we keep it
            if( pCall->getScore( ) > dMinScore )
                pRet->vContent.push_back( pCall );
#if ANALYZE_FILTERS
        std::lock_guard<std::mutex> xGuard( xLock );
        uiFilterTotal += pCalls->vContent.size( );
        uiFilterKept += pRet->vContent.size( );
#endif
        return pRet;
    } // method

}; // class

} // namespace libMA

#ifdef WITH_PYTHON
void exportSweepSvJump( py::module& rxPyModuleId );
#endif
