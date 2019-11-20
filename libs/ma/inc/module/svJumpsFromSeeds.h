/**
 * @file svJumpsFromSeeds.h
 * @brief Implements a way to compute SV-jumps from seeds.
 * @author Markus Schmidt
 */
#pragma once

#include "container/segment.h"
#include "container/svDb.h"
#include "container/svJump.h"
#include "module/binarySeeding.h"
#include "module/hashMapSeeding.h"
#include "module/module.h"
#include "module/needlemanWunsch.h"

namespace libMA
{
class PerfectMatch;

#define complement( x ) ( uint8_t ) NucSeq::nucleotideComplement( x )


/**
 * @brief Computes Sv-Jumps from a given seed set
 * @note WARNING: DO USE EACH INSTANCE OF THIS MODULE ONLY ONCE IN THE COMPUTATIONAL GRAPH
 */
class SvJumpsFromSeeds : public Module<ContainerVector<SvJump>, false, SegmentVector, Pack, FMIndex, NucSeq>
{
  public:
    const std::shared_ptr<Presetting> pSelectedSetting;
    const size_t uiMinSeedSizeSV;
    const size_t uiMaxAmbiguitySv;
    const bool bDoDummyJumps;
    const size_t uiMinDistDummy;
    HashMapSeeding xHashMapSeeder;
    SeedLumping xSeedLumper;
    // @todo there should be a container for the current sequencer run;
    // this way this module would be free from keeping internal data and could be used for multiple instances in the
    // graph...
    int64_t iSequencerId;
    std::shared_ptr<SV_DB> pDb;

    std::mutex xLock;
    size_t uiNumSeedsEliminatedAmbiguityFilter = 0;
    size_t uiNumSeedsKeptAmbiguityFilter = 0;

    SV_DB::ContigCovTable::CovInserter xCoverageInserter;

    /// used to indicate that there is no seed for on of the parameters in the recursive call.
    Seed xDummySeed;

    /**
     * @brief Initialize a SvJumpsFromSeeds Module
     */
    SvJumpsFromSeeds( const ParameterSetManager& rParameters, int64_t iSequencerId, std::shared_ptr<SV_DB> pDb,
                      std::shared_ptr<Pack> pRefSeq )
        : pSelectedSetting( rParameters.getSelected( ) ),
          uiMinSeedSizeSV( pSelectedSetting->xMinSeedSizeSV->get( ) ),
          uiMaxAmbiguitySv( pSelectedSetting->xMaxAmbiguitySv->get( ) ),
          bDoDummyJumps( pSelectedSetting->xDoDummyJumps->get( ) ),
          uiMinDistDummy( pSelectedSetting->xMinDistDummy->get( ) ),
          xHashMapSeeder( rParameters ),
          xSeedLumper( rParameters ),
          iSequencerId( iSequencerId ),
          pDb( pDb ),
          xCoverageInserter( iSequencerId, pRefSeq, pDb )
    {
        xBinarySeeding.bDisableHeuristics = true;
    } // constructor

    ~SvJumpsFromSeeds( )
    {
        size_t uiTotal = uiNumSeedsKeptAmbiguityFilter + uiNumSeedsEliminatedAmbiguityFilter;
        if( uiTotal > 0 )
            std::cout << "~SvJumpsFromSeeds: ambiguity filter kept and eliminated " << uiNumSeedsKeptAmbiguityFilter
                      << " and " << uiNumSeedsEliminatedAmbiguityFilter << " seeds respectively. " << std::endl
                      << "\tThats " << ( 1000 * uiNumSeedsKeptAmbiguityFilter / uiTotal ) / 10.0 << "% and "
                      << ( 1000 * uiNumSeedsEliminatedAmbiguityFilter / uiTotal ) / 10.0 << "% respectively."
                      << std::endl;
    } // destructor

    /**
     * @details
     * shall return a rectange (reference pos, query pos, width [on reference], height [on query])
     */
    std::tuple<nucSeqIndex, nucSeqIndex, nucSeqIndex, nucSeqIndex> getPositionsForSeeds( Seed& rLast, Seed& rNext,
                                                                                         nucSeqIndex uiQSize );

    void makeJumpsByReseedingRecursive( Seed& rLast, Seed& rNext, std::shared_ptr<NucSeq> pQuery,
                                        std::shared_ptr<Pack> pRefSeq, std::shared_ptr<ContainerVector<SvJump>>& pRet );


    virtual std::shared_ptr<ContainerVector<SvJump>> EXPORTED execute( std::shared_ptr<SegmentVector> pSegments,
                                                                       std::shared_ptr<Pack>
                                                                           pRefSeq,
                                                                       std::shared_ptr<FMIndex>
                                                                           pFM_index,
                                                                       std::shared_ptr<NucSeq>
                                                                           pQuery );

    void commit( )
    {
        xCoverageInserter.commit( );
    } // method
}; // class

}; // namespace libMA

#ifdef WITH_PYTHON
/**
 * @brief exports the SvJumpsFromSeeds @ref Module "module" to python.
 * @ingroup export
 */
void exportSvJumpsFromSeeds( py::module& rxPyModuleId );
#endif