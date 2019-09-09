/**
 * @file svDb.h
 * @details
 * The database interface for the structural variant caller
 */
#pragma once

#include "container/container.h"
#include "container/nucSeq.h"
#include "container/pack.h"
#include "container/soc.h"
#include "container/svJump.h"
#include "module/fileReader.h"
#include "module/module.h"
#include "util/exception.h"
#include "util/sqlite3.h"
#include "util/system.h"
#include <chrono>
#include <ctime>
#include <iomanip>

namespace libMA
{

class SV_DB : public Container
{
  public:
    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<std::string // sequencer name
                                                     >
        TP_SEQUENCER_TABLE;
    class SequencerTable : public TP_SEQUENCER_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SequencerTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SEQUENCER_TABLE( *pDatabase, // the database where the table resides
                                  "sequencer_table", // name of the table in the database
                                  // column definitions of the table
                                  std::vector<std::string>{"name"},
                                  // constraints for table
                                  std::vector<std::string>{"UNIQUE (name)"} ),
              pDatabase( pDatabase )
        {
            // pDatabase->execDML( "CREATE INDEX IF NOT EXISTS sequencer_id_index ON sequencer_table (id)" );
        } // default constructor

        inline int64_t insertSequencer( std::string& sSequencerName )
        {
            return xInsertRow( sSequencerName );
        } // method
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sequencer_id
                                                     int64_t, // contig_nr
                                                     int64_t // num_generated_nt
                                                     >
        TP_CONTIG_COV_TABLE;
    class ContigCovTable : public TP_CONTIG_COV_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        CppSQLiteExtStatement xIncNt;
        CppSQLiteExtQueryStatement<int64_t> xGetNumNt;
        bool bPrintedCovList = false;

      public:
        ContigCovTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_CONTIG_COV_TABLE(
                  *pDatabase, // the database where the table resides
                  "contig_cov_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"sequencer_id", "contig_nr", "num_generated_nt"},
                  // constraints for table
                  std::vector<std::string>{"UNIQUE (sequencer_id, contig_nr)",
                                           "FOREIGN KEY (sequencer_id) REFERENCES sequencer_table(id)"} ),
              pDatabase( pDatabase ),
              xIncNt( *pDatabase,
                      "UPDATE contig_cov_table "
                      "SET num_generated_nt = num_generated_nt + ? "
                      "WHERE sequencer_id == ? "
                      "AND contig_nr == ? " ),
              xGetNumNt( *pDatabase,
                         "SELECT num_generated_nt "
                         "FROM contig_cov_table "
                         "WHERE sequencer_id == ? "
                         "ORDER BY contig_nr " )
        {
            // pDatabase->execDML( "CREATE INDEX IF NOT EXISTS sequencer_id_index ON sequencer_table (id)" );
        } // default constructor

        inline int64_t insert( int64_t iSequencerId, int64_t iContigId )
        {
            return xInsertRow( iSequencerId, iContigId, 0 );
        } // method

        inline void insert( int64_t iSequencerId, std::shared_ptr<Pack> pPack )
        {
            for( size_t uiI = 0; uiI < pPack->uiNumContigs( ); uiI++ )
                insert( iSequencerId, uiI );
        } // method

        inline void incrementNt( int64_t iSequencerId, int64_t iContigId, int64_t iAmount )
        {
            xIncNt.bindAndExecute( iAmount, iSequencerId, iContigId );
        } // method

        inline std::vector<int64_t> getNumNt( int64_t iSequencerId )
        {
            return xGetNumNt.executeAndStoreInVector<0>( iSequencerId );
        } // method

        inline std::vector<double> getEstimatedCoverageList( int64_t iSequencerId, std::shared_ptr<Pack> pPack )
        {
            std::vector<double> vRet;
            auto vNumNt = this->getNumNt( iSequencerId );
            assert( vNumNt.size( ) == pPack->uiNumContigs( ) );
            vRet.reserve( vNumNt.size( ) );
            if( !bPrintedCovList )
            {
                int64_t iTotal = 0;
                for( size_t uiI = 0; uiI < vNumNt.size( ); uiI++ )
                    iTotal += vNumNt[ uiI ];
                std::cout << "estimated coverage per contig (showing >= 3x):" << std::endl;
                std::cout << "contig_id\tcoverage\tnum_nt\t%" << std::endl;
                for( size_t uiI = 0; uiI < vNumNt.size( ); uiI++ )
                    if( vNumNt[ uiI ] / (double)pPack->xVectorOfSequenceDescriptors[ uiI ].uiLengthUnpacked >= 3 &&
                        100 * vNumNt[ uiI ] >= iTotal )
                        std::cout << uiI << "\t"
                                  << ( (int)10 * vNumNt[ uiI ] /
                                       (double)pPack->xVectorOfSequenceDescriptors[ uiI ].uiLengthUnpacked ) /
                                         10.0
                                  << "x\t" << vNumNt[ uiI ] << "\t" << ( 100 * vNumNt[ uiI ] ) / iTotal << "%"
                                  << std::endl;
                std::cout << std::endl;
                bPrintedCovList = true;
            } // if
            for( size_t uiI = 0; uiI < vNumNt.size( ); uiI++ )
                vRet.push_back( vNumNt[ uiI ] /
                                ( (double)pPack->xVectorOfSequenceDescriptors[ uiI ].uiLengthUnpacked ) );
            return vRet;
        } // method

        class CovInserter
        {
          public:
            int64_t iSequencerId;
            std::shared_ptr<SV_DB> pDb;
            std::shared_ptr<Pack> pPack;
            std::vector<int64_t> vNumNts;
            std::mutex xLock;

            CovInserter( int64_t iSequencerId, std::shared_ptr<Pack> pPack, std::shared_ptr<SV_DB> pDb )
                : iSequencerId( iSequencerId ), pDb( pDb ), pPack( pPack ), vNumNts( pPack->uiNumContigs( ) )
            {} // constructor

            void commit( )
            {
                std::lock_guard<std::mutex> xGuard( *pDb->pWriteLock );
                for( size_t uiI = 0; uiI < vNumNts.size( ); uiI++ )
                    if( vNumNts[ uiI ] > 0 )
                    {
                        pDb->pContigCovTable->incrementNt( iSequencerId, uiI, vNumNts[ uiI ] );
                        vNumNts[ uiI ] = 0;
                    } // if
            } // function

            ~CovInserter( )
            {
                commit( );
            } // deconstructor

            /// @brief seeds need to be sorted by query pos
            inline void insert( Seeds& rSeeds, nucSeqIndex uiQlen )
            {
                for( size_t uiI = 0; uiI < rSeeds.size( ); uiI++ )
                {
                    // size of this seed
                    int64_t iSize = rSeeds[ uiI ].size( );

#if 1 // 1 -> add gap in between seeds to estimation / 0 -> don't
      // add gap to previous seed or start of query
                    if( uiI == 0 )
                        iSize += rSeeds[ uiI ].start( );
                    else if( rSeeds[ uiI ].start( ) < rSeeds[ uiI - 1 ].end( ) )
                        iSize += ( rSeeds[ uiI - 1 ].end( ) - rSeeds[ uiI ].start( ) ) / 2;

                    // add gap to next seed or end of query
                    if( uiI + 1 == rSeeds.size( ) )
                        iSize += uiQlen - rSeeds[ uiI ].end( );
                    else if( rSeeds[ uiI ].end( ) < rSeeds[ uiI + 1 ].start( ) )
                        iSize += ( rSeeds[ uiI + 1 ].start( ) - rSeeds[ uiI ].end( ) ) / 2;
#endif

                    // increase the count
                    size_t uiIdx = pPack->uiSequenceIdForPosition( rSeeds[ uiI ].start_ref( ) );
                    {
                        std::lock_guard<std::mutex> xGuard( xLock );
                        vNumNts[ uiIdx ] += iSize;
                    } // scope for xGuard
                } // for
            } // method
        }; // class
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sequencer id (foreign key)
                                                     std::string, // read name
                                                     NucSeqSql // read sequence
                                                     >
        TP_READ_TABLE;
    class ReadTable : public TP_READ_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        bool bDoDuplicateWarning = true;

      public:
        CppSQLiteExtQueryStatement<int32_t> xGetReadId;
        CppSQLiteExtQueryStatement<NucSeqSql> xGetRead;

        ReadTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_READ_TABLE( *pDatabase, // the database where the table resides
                             "read_table", // name of the table in the database
                             // column definitions of the table
                             std::vector<std::string>{"sequencer_id", "name", "sequence"},
                             // constraints for table
                             std::vector<std::string>{"FOREIGN KEY (sequencer_id) REFERENCES sequencer_table(id) "} ),
              pDatabase( pDatabase ),
              xGetReadId( *pDatabase, "SELECT id FROM read_table WHERE sequencer_id == ? AND name == ? " ),
              xGetRead( *pDatabase, "SELECT sequence FROM read_table WHERE id == ? " )
        {} // default constructor

        inline int64_t insertRead( int64_t uiSequencerId, std::shared_ptr<NucSeq> pRead )
        {
            return xInsertRow( uiSequencerId, pRead->sName, NucSeqSql( pRead ) );
        } // method

        inline std::shared_ptr<NucSeq> getRead( int64_t iId )
        {
            auto xRes = xGetRead.scalar( iId );
            xRes.pNucSeq->iId = iId;
            return xRes.pNucSeq;
        } // method
    }; // class

    typedef CppSQLiteExtTable<int64_t, // first read (foreign key)
                              int64_t // second read (foreign key)
                              >
        TP_PAIRED_READ_TABLE;
    class PairedReadTable : public TP_PAIRED_READ_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        std::shared_ptr<ReadTable> pReadTable;

      public:
        PairedReadTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase, std::shared_ptr<ReadTable> pReadTable );

        ~PairedReadTable( )
        {} // deconstructor

        inline std::pair<int64_t, int64_t> insertRead( int64_t uiSequencerId, std::shared_ptr<NucSeq> pReadA,
                                                       std::shared_ptr<NucSeq> pReadB )
        {
            int64_t uiReadIdA = pReadTable->insertRead( uiSequencerId, pReadA );
            int64_t uiReadIdB = pReadTable->insertRead( uiSequencerId, pReadB );
            xInsertRow( uiReadIdA, uiReadIdB );
            return std::make_pair( uiReadIdA, uiReadIdB );
        } // method
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<std::string, // name
                                                     std::string, // desc
                                                     int64_t // timestamp
                                                     >
        TP_NAME_DESC_TABLE;
    class NameDescTable : public TP_NAME_DESC_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        const std::string sTableName;
        CppSQLiteExtQueryStatement<int64_t> xDelete;
        CppSQLiteExtQueryStatement<int64_t> xGetId;
        CppSQLiteExtQueryStatement<std::string, std::string, int64_t> xGetName;
        CppSQLiteExtQueryStatement<uint32_t> xNum;
        CppSQLiteExtQueryStatement<uint32_t> xExists;
        CppSQLiteExtQueryStatement<uint32_t> xNameExists;
        CppSQLiteExtQueryStatement<int64_t> xNewestUnique;

      public:
        NameDescTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase, const std::string sTableName )
            : TP_NAME_DESC_TABLE( *pDatabase, // the database where the table resides
                                  sTableName, // name of the table in the database
                                  // column definitions of the table
                                  std::vector<std::string>{"name", "desc", "time_stamp"} ),
              pDatabase( pDatabase ),
              sTableName( sTableName ),
              xDelete( *pDatabase, ( "DELETE FROM " + sTableName + " WHERE name == ?" ).c_str( ) ),
              xGetId(
                  *pDatabase,
                  ( "SELECT id FROM " + sTableName + " WHERE name == ? ORDER BY time_stamp ASC LIMIT 1" ).c_str( ) ),
              xGetName( *pDatabase,
                        ( "SELECT name, desc, time_stamp FROM " + sTableName + " WHERE id == ?" ).c_str( ) ),
              xNum( *pDatabase, ( "SELECT COUNT(*) FROM " + sTableName ).c_str( ) ),
              xExists( *pDatabase, ( "SELECT COUNT(*) FROM " + sTableName + " WHERE id == ?" ).c_str( ) ),
              xNameExists( *pDatabase, ( "SELECT COUNT(*) FROM " + sTableName + " WHERE name == ?" ).c_str( ) ),
              xNewestUnique(
                  *pDatabase,
                  ( "SELECT id FROM " + sTableName + " AS outer WHERE ( SELECT COUNT(*) FROM " + sTableName +
                    " AS inner WHERE inner.name = outer.name AND inner.time_stamp >= outer.time_stamp ) < ?" )
                      .c_str( ) )
        {} // default constructor

        inline void deleteName( std::string& rS )
        {
            xDelete.bindAndExecQuery<>( rS );
            // vDump( std::cout );
        } // method

        inline int64_t getId( std::string& rS )
        {
            return xGetId.scalar( rS );
        } // method

        inline bool exists( int64_t iId )
        {
            return xExists.scalar( iId ) > 0;
        } // method

        inline bool nameExists( std::string sName )
        {
            return xNameExists.scalar( sName ) > 0;
        } // method

        inline std::string getName( int64_t iId )
        {
            return std::get<0>( xGetName.vExecuteAndReturnIterator( iId ).get( ) );
        } // method

        inline std::string getDesc( int64_t iId )
        {
            return std::get<1>( xGetName.vExecuteAndReturnIterator( iId ).get( ) );
        } // method

        inline std::string getDate( int64_t iId )
        {
            auto now_c = (std::time_t)std::get<2>( xGetName.vExecuteAndReturnIterator( iId ).get( ) );
            std::stringstream ss;
#ifdef _MSC_VER
#pragma warning( suppress : 4996 ) // @todo find another way to do this
            ss << std::put_time( std::localtime( &now_c ), "%c" );
#else
            ss << std::put_time( std::localtime( &now_c ), "%c" );
#endif
            return ss.str( );
        } // method

        inline uint32_t size( )
        {
            return xNum.scalar( );
        } // method

        inline int64_t insert( std::string sName, std::string sDesc )
        {
            return this->xInsertRow(
                sName, sDesc, (int64_t)std::chrono::system_clock::to_time_t( std::chrono::system_clock::now( ) ) );
        } // method

        inline std::vector<int64_t> getNewestUnique( uint32_t uiNum )
        {
            return xNewestUnique.executeAndStoreInVector<0>( uiNum );
        } // method
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<std::string, // name
                                                     std::string, // desc
                                                     int64_t, // timestamp
                                                     int64_t // sv_jump_run_id
                                                     >
        TP_SV_CALLER_RUN_TABLE;
    class SvCallerRunTable : public TP_SV_CALLER_RUN_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        CppSQLiteExtQueryStatement<int64_t> xDelete;
        CppSQLiteExtQueryStatement<int64_t> xGetId;
        CppSQLiteExtQueryStatement<std::string, std::string, int64_t, int64_t> xGetName;
        CppSQLiteExtQueryStatement<uint32_t> xNum;
        CppSQLiteExtQueryStatement<uint32_t> xExists;
        CppSQLiteExtQueryStatement<uint32_t> xNameExists;
        CppSQLiteExtQueryStatement<int64_t> xNewestUnique;
        CppSQLiteExtStatement xInsertRow2;

      public:
        SvCallerRunTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_CALLER_RUN_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_caller_run_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"name", "desc", "time_stamp", "sv_jump_run_id"},
                  // constraints for table
                  std::vector<std::string>{"FOREIGN KEY (sv_jump_run_id) REFERENCES sv_jump_run_table(id)"} ),
              pDatabase( pDatabase ),
              xDelete( *pDatabase, "DELETE FROM sv_caller_run_table WHERE name == ?" ),
              xGetId( *pDatabase,
                      "SELECT id FROM sv_caller_run_table WHERE name == ? ORDER BY time_stamp ASC LIMIT 1" ),
              xGetName( *pDatabase,
                        "SELECT name, desc, time_stamp, sv_jump_run_id FROM sv_caller_run_table WHERE id == ?" ),
              xNum( *pDatabase, "SELECT COUNT(*) FROM sv_caller_run_table " ),
              xExists( *pDatabase, "SELECT COUNT(*) FROM sv_caller_run_table WHERE id == ?" ),
              xNameExists( *pDatabase, "SELECT COUNT(*) FROM sv_caller_run_table WHERE name == ?" ),
              xNewestUnique(
                  *pDatabase,
                  "SELECT id FROM sv_caller_run_table AS outer WHERE ( SELECT COUNT(*) FROM sv_caller_run_table AS "
                  "inner WHERE inner.name = outer.name AND inner.time_stamp >= outer.time_stamp ) < ? "
                  "AND desc = ? " ),
              xInsertRow2( *pDatabase,
                           "INSERT INTO sv_caller_run_table (id, name, desc, time_stamp, sv_jump_run_id) "
                           "VALUES (NULL, ?, ?, ?, NULL)" )
        {} // default constructor

        inline void deleteName( std::string& rS )
        {
            xDelete.bindAndExecQuery<>( rS );
            // vDump( std::cout );
        } // method

        inline int64_t getId( std::string& rS )
        {
            return xGetId.scalar( rS );
        } // method

        inline bool exists( int64_t iId )
        {
            return xExists.scalar( iId ) > 0;
        } // method

        inline bool nameExists( std::string sName )
        {
            return xNameExists.scalar( sName ) > 0;
        } // method

        inline std::string getName( int64_t iId )
        {
            return std::get<0>( xGetName.vExecuteAndReturnIterator( iId ).get( ) );
        } // method

        inline std::string getDesc( int64_t iId )
        {
            return std::get<1>( xGetName.vExecuteAndReturnIterator( iId ).get( ) );
        } // method

        inline int64_t getSvJumpRunId( int64_t iId )
        {
            return std::get<3>( xGetName.vExecuteAndReturnIterator( iId ).get( ) );
        } // method

        inline std::string getDate( int64_t iId )
        {
            auto now_c = (std::time_t)std::get<2>( xGetName.vExecuteAndReturnIterator( iId ).get( ) );
            std::stringstream ss;
#ifdef _MSC_VER
#pragma warning( suppress : 4996 ) // @todo find another way to do this
            ss << std::put_time( std::localtime( &now_c ), "%c" );
#else
            ss << std::put_time( std::localtime( &now_c ), "%c" );
#endif
            return ss.str( );
        } // method

        inline uint32_t size( )
        {
            return xNum.scalar( );
        } // method

        inline int64_t insert( std::string sName, std::string sDesc, int64_t uiJumpRunId )
        {
            if( uiJumpRunId < 0 )
            {
                this->xInsertRow2.bindAndExecute(
                    sName, sDesc, (int64_t)std::chrono::system_clock::to_time_t( std::chrono::system_clock::now( ) ) );
                // get the rowid = primary key of the inserted row
                return static_cast<int64_t>( pDatabase->lastRowId( ) );
            }
            return this->xInsertRow( sName, sDesc,
                                     (int64_t)std::chrono::system_clock::to_time_t( std::chrono::system_clock::now( ) ),
                                     uiJumpRunId );
        } // method

        inline std::vector<int64_t> getNewestUnique( uint32_t uiNum, std::string sDesc )
        {
            return xNewestUnique.executeAndStoreInVector<0>( uiNum, sDesc );
        } // method
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sv_jump_run_id (foreign key)
                                                     int64_t, // read_id (foreign key)
                                                     int64_t, // sort_pos_start
                                                     int64_t, // sort_pos_end
                                                     uint32_t, // from_pos
                                                     uint32_t, // to_pos
                                                     uint32_t, // query_from
                                                     uint32_t, // query_to
                                                     uint32_t, // num_supporting_nt
                                                     bool, // from_forward @todo save space by compressing booleans?
                                                     bool, // to_forward
                                                     bool // from_seed_start
                                                     >
        TP_SV_JUMP_TABLE;
    class SvJumpTable : public TP_SV_JUMP_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        CppSQLiteExtQueryStatement<uint32_t> xQuerySize;
        CppSQLiteExtQueryStatement<int64_t> xDeleteRun;

      public:
        SvJumpTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_JUMP_TABLE( *pDatabase, // the database where the table resides
                                "sv_jump_table", // name of the table in the database
                                // column definitions of the table
                                std::vector<std::string>{"sv_jump_run_id", "read_id", "sort_pos_start", "sort_pos_end",
                                                         "from_pos", "to_pos", "query_from", "query_to",
                                                         "num_supporting_nt", "from_forward", "to_forward",
                                                         "from_seed_start"},
                                // constraints for table
                                std::vector<std::string>{
                                    "FOREIGN KEY (sv_jump_run_id) REFERENCES sv_jump_run_table(id) ON DELETE CASCADE",
                                    "FOREIGN KEY (read_id) REFERENCES read_table(id)"} ),
              pDatabase( pDatabase ),
              xQuerySize( *pDatabase, "SELECT COUNT(*) FROM sv_jump_table" ),
              xDeleteRun( *pDatabase, "DELETE FROM sv_jump_table WHERE sv_jump_run_id IN ( SELECT id FROM "
                                      "sv_jump_run_table WHERE name == ?)" )
        {} // default constructor

        inline void createIndices( int64_t uiRun )
        {
            // https://www.sqlite.org/queryplanner.html -> 3.2. Searching And Sorting With A Covering Index
            // index intended for the sweep over the start of all sv-rectangles
            // interestingly sv_jump_run_id nneds to be part of the index even if it's in the condition...
            pDatabase->execDML( ( "CREATE INDEX IF NOT EXISTS sv_jump_table_sort_index_start_" +
                                  std::to_string( uiRun ) +
                                  " ON sv_jump_table"
                                  "(sort_pos_start, from_pos, to_pos, query_from, query_to, from_forward,"
                                  " to_forward, from_seed_start, num_supporting_nt, id, read_id, sv_jump_run_id) "
                                  "WHERE sv_jump_run_id == " +
                                  std::to_string( uiRun ) )
                                    .c_str( ) );
            // index intended for the sweep over the end of all sv-rectangles
            pDatabase->execDML( ( "CREATE INDEX IF NOT EXISTS sv_jump_table_sort_index_end_" + std::to_string( uiRun ) +
                                  " ON sv_jump_table"
                                  "(sort_pos_end, from_pos, to_pos, query_from, query_to, from_forward,"
                                  " to_forward, from_seed_start, num_supporting_nt, id, read_id, sv_jump_run_id) "
                                  "WHERE sv_jump_run_id == " +
                                  std::to_string( uiRun ) )
                                    .c_str( ) );
        } // method

        inline uint32_t numJumps( )
        {
            return xQuerySize.scalar( );
        } // method

        inline void deleteRun( std::string& rS )
        {
            xDeleteRun.bindAndExecQuery<>( rS );
        } // method
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<std::string, // regex
                                                     uint32_t // state
                                                     >
        TP_SV_CALL_REG_EX_TABLE;
    class SvCallRegExTable : public TP_SV_CALL_REG_EX_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SvCallRegExTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_CALL_REG_EX_TABLE( *pDatabase, // the database where the table resides
                                       "sv_call_reg_ex_table", // name of the table in the database
                                       // column definitions of the table
                                       std::vector<std::string>{"regex", "state"} ),
              pDatabase( pDatabase )
        {} // default constructor
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sv_caller_run_id (foreign key)
                                                     uint32_t, // from_pos
                                                     uint32_t, // to_pos
                                                     uint32_t, // from_size
                                                     uint32_t, // to_size
                                                     bool, // switch_strand
                                                     NucSeqSql, // inserted_sequence
                                                     uint32_t, // supporting_nt
                                                     uint32_t, // coverage
                                                     int64_t // regex_id
                                                     >
        TP_SV_CALL_TABLE;
    class SvCallTable : private TP_SV_CALL_TABLE
    {

#if 0
        typedef CppSQLiteExtTable<int64_t // call_id
                                  >
            TP_RECONSTRUCTION_TABLE;
        class ReconstructionTable : public TP_RECONSTRUCTION_TABLE
        {
            std::shared_ptr<CppSQLiteDBExtended> pDatabase;

          public:
            ReconstructionTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
                : TP_RECONSTRUCTION_TABLE( *pDatabase, // the database where the table resides
                                           "reconstruction_table", // name of the table in the database
                                           // column definitions of the table
                                           std::vector<std::string>{"call_id"},
                                           false ),
                  pDatabase( pDatabase )
            {
                pDatabase->execDML( "CREATE INDEX IF NOT EXISTS reconstruction_table_index "
                                    "ON reconstruction_table (call_id) " );
            } // default constructor

        }; // class
#endif

        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        // std::shared_ptr<ReconstructionTable> pReconstructionTable;
        class RTreeIndex
        {
          public:
            RTreeIndex( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            {
                // create a R*tree index
                if( pDatabase->eDatabaseOpeningMode == eCREATE_DB )
                {
                    /* We drop the table in the case that it exists already. */
                    pDatabase->execDML( "DROP TABLE IF EXISTS sv_call_r_tree" );
                    pDatabase->execDML( "CREATE VIRTUAL TABLE sv_call_r_tree USING rtree_i32( "
                                        "       id, " // key from sv_call_table
                                        "       run_id_a, run_id_b, " // run id -> has to be a dimension for it to work
                                        "       minX, maxX, " // start & end of from positions
                                        "       minY, maxY " // start & end of to positions
                                        "   )" );
                } // if
            } // constructor
        }; // class
        RTreeIndex xIndex; // merely required for calling the constructor
        CppSQLiteExtInsertStatement<int64_t, int64_t, int64_t, uint32_t, uint32_t, uint32_t, uint32_t> xInsertRTree;
        CppSQLiteExtQueryStatement<uint32_t> xQuerySize;
        CppSQLiteExtQueryStatement<uint32_t> xQuerySizeSpecific;
        CppSQLiteExtQueryStatement<int64_t, double, uint32_t, uint32_t, uint32_t, uint32_t, bool> xNumOverlaps;
        CppSQLiteExtQueryStatement<int64_t> xNumOverlapsHelper1;
        CppSQLiteExtQueryStatement<int64_t> xNumOverlapsHelper2;
        CppSQLiteExtQueryStatement<int64_t> xCallArea;
        CppSQLiteExtQueryStatement<double> xMaxScore;
        CppSQLiteExtQueryStatement<double> xMinScore;
        CppSQLiteExtQueryStatement<int64_t, bool, uint32_t, uint32_t, NucSeqSql, uint32_t> xNextCallForwardContext;
        CppSQLiteExtQueryStatement<int64_t, bool, uint32_t, uint32_t, NucSeqSql, uint32_t> xNextCallBackwardContext;
        CppSQLiteExtStatement xSetCoverageForCall;
        CppSQLiteExtStatement xDeleteCall1, xDeleteCall2;
        CppSQLiteExtStatement xUpdateCall, xUpdateRTree;

      public:
        SvCallTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_CALL_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_call_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"sv_caller_run_id", "from_pos", "to_pos", "from_size", "to_size",
                                           "switch_strand", "inserted_sequence", "supporting_nt", "coverage",
                                           "regex_id"},
                  // constraints for table
                  std::vector<std::string>{
                      "FOREIGN KEY (sv_caller_run_id) REFERENCES sv_caller_run_table(id) ON DELETE CASCADE",
                      "FOREIGN KEY (regex_id) REFERENCES sv_call_reg_ex_table(id) ON DELETE SET NULL"} ),
              pDatabase( pDatabase ),
              // pReconstructionTable( std::make_shared<ReconstructionTable>( pDatabase ) ),
              xIndex( pDatabase ),
              xInsertRTree( *pDatabase, "sv_call_r_tree", false ),
              xQuerySize( *pDatabase, "SELECT COUNT(*) FROM sv_call_table" ),
              xQuerySizeSpecific( *pDatabase, "SELECT COUNT(*) FROM sv_call_table, sv_call_r_tree "
                                              "WHERE sv_call_table.id == sv_call_r_tree.id "
                                              "AND sv_call_r_tree.run_id_a >= ? " // dim 1
                                              "AND sv_call_r_tree.run_id_b <= ? " // dim 1
                                              "AND (supporting_nt*1.0)/coverage >= ? " ),
              xNumOverlaps( *pDatabase,
                            // each inner call can overlap an outer call at most once
                            "SELECT id, supporting_nt*1.0/coverage, from_pos, from_size, to_pos, to_size, "
                            "       switch_strand "
                            "FROM sv_call_table "
                            "WHERE sv_caller_run_id = ? "
                            "AND supporting_nt*1.0/coverage >= ? " ),
              xNumOverlapsHelper1( *pDatabase,
                                   // make sure that inner overlaps the outer:
                                   "SELECT outer.id "
                                   "FROM sv_call_table AS outer, sv_call_r_tree AS idx_outer "
                                   "WHERE outer.id == idx_outer.id "
                                   "AND idx_outer.run_id_b >= ? " // dim 1
                                   "AND idx_outer.run_id_a <= ? " // dim 1
                                   "AND idx_outer.maxX >= ? " // dim 2
                                   "AND idx_outer.minX <= ? " // dim 2
                                   "AND idx_outer.maxY >= ? " // dim 3
                                   "AND idx_outer.minY <= ? " // dim 3
                                   "AND outer.switch_strand == ? "
                                   "LIMIT 1 " ),
              xNumOverlapsHelper2( *pDatabase,
                                   // make sure that inner does not overlap with any other call with higher score
                                   "SELECT inner2.id "
                                   "FROM sv_call_table AS inner2, sv_call_r_tree AS idx_inner2 "
                                   "WHERE inner2.id == idx_inner2.id "
                                   "AND idx_inner2.id != ? "
                                   "AND (inner2.supporting_nt*1.0)/inner2.coverage >= ? "
                                   "AND idx_inner2.run_id_b >= ? " // dim 1
                                   "AND idx_inner2.run_id_a <= ? " // dim 1
                                   "AND idx_inner2.maxX >= ? " // dim 2
                                   "AND idx_inner2.minX <= ? " // dim 2
                                   "AND idx_inner2.maxY >= ? " // dim 3
                                   "AND idx_inner2.minY <= ? " // dim 3
                                   "AND inner2.switch_strand == ? "
                                   "LIMIT 1 " ),
              xCallArea( *pDatabase,
                         "SELECT SUM( from_size * to_size ) FROM sv_call_table, sv_call_r_tree "
                         "WHERE sv_call_table.id == sv_call_r_tree.id "
                         "AND sv_call_r_tree.run_id_a >= ? " // dim 1
                         "AND sv_call_r_tree.run_id_b <= ? " // dim 1
                         "AND (supporting_nt*1.0)/coverage >= ? " ),
              xMaxScore( *pDatabase,
                         "SELECT supporting_nt*1.0/coverage FROM sv_call_table, sv_call_r_tree "
                         "WHERE sv_call_table.id == sv_call_r_tree.id "
                         "AND sv_call_r_tree.run_id_a >= ? " // dim 1
                         "AND sv_call_r_tree.run_id_b <= ? " // dim 1
                         "ORDER BY (supporting_nt*1.0)/coverage DESC LIMIT 1 " ),
              xMinScore( *pDatabase,
                         "SELECT (supporting_nt*1.0)/coverage FROM sv_call_table, sv_call_r_tree "
                         "WHERE sv_call_table.id == sv_call_r_tree.id "
                         "AND sv_call_r_tree.run_id_a >= ? " // dim 1
                         "AND sv_call_r_tree.run_id_b <= ? " // dim 1
                         "ORDER BY (supporting_nt*1.0)/coverage ASC LIMIT 1 " ),
              xNextCallForwardContext(
                  *pDatabase,
                  "SELECT sv_call_table.id, switch_strand, to_pos, to_size, inserted_sequence, from_pos + from_size "
                  "FROM sv_call_table "
                  "WHERE sv_call_table.sv_caller_run_id == ? " // dim 1
                  "AND sv_call_table.from_pos >= ? " // dim 2
#if 0
                  "AND NOT EXISTS ( "
                  "  SELECT * "
                  "  FROM reconstruction_table "
                  "  WHERE sv_call_table.id == reconstruction_table.call_id "
                  ") "
#endif
                  "ORDER BY sv_call_table.from_pos ASC "
                  "LIMIT 1 " ),
              xNextCallBackwardContext( *pDatabase,
                                        "SELECT sv_call_table.id, switch_strand, from_pos, from_size, "
                                        "       inserted_sequence, to_pos "
                                        "FROM sv_call_table "
                                        "WHERE sv_call_table.sv_caller_run_id == ? " // dim 1
                                        "AND sv_call_table.to_pos <= ? " // dim 2
#if 0
                                        "AND NOT EXISTS ( "
                                        "  SELECT * "
                                        "  FROM reconstruction_table "
                                        "  WHERE sv_call_table.id == reconstruction_table.call_id "
                                        ") "
#endif
                                        "ORDER BY sv_call_table.to_pos DESC "
                                        "LIMIT 1 " ),
              xSetCoverageForCall( *pDatabase,
                                   "UPDATE sv_call_table "
                                   "SET coverage = ? "
                                   "WHERE id == ?" ),
              xDeleteCall1( *pDatabase,
                            "DELETE FROM sv_call_r_tree "
                            "WHERE id == ? " ),
              xDeleteCall2( *pDatabase,
                            "DELETE FROM sv_call_table "
                            "WHERE id == ? " ),
              xUpdateCall( *pDatabase,
                           "UPDATE sv_call_table "
                           "SET from_pos = ?, "
                           "    to_pos = ?, "
                           "    from_size = ?, "
                           "    to_size = ?, "
                           "    switch_strand = ?, "
                           "    inserted_sequence = ?, "
                           "    supporting_nt = ?, "
                           "    coverage = ? "
                           "WHERE id == ? " ),
              xUpdateRTree( *pDatabase,
                            "UPDATE sv_call_r_tree "
                            "SET run_id_a = ?, "
                            "    run_id_b = ?, "
                            "    minX = ?, "
                            "    maxX = ?, "
                            "    minY = ?, "
                            "    maxY = ? "
                            "WHERE id == ? " )
        {} // default constructor

        inline void addScoreIndex( int64_t iCallerRunId )
        {
            CppSQLiteExtStatement( *pDatabase,
                                   ( "CREATE INDEX IF NOT EXISTS sv_call_table_score_index_" +
                                     std::to_string( iCallerRunId ) +
                                     " ON sv_call_table ((supporting_nt*1.0)/coverage) "
                                     "WHERE sv_caller_run_id == " +
                                     std::to_string( iCallerRunId ) )
                                       .c_str( ) )
                .execDML( );
        } // method

        inline uint32_t numCalls( )
        {
            return xQuerySize.scalar( );
        } // method

        inline uint32_t numCalls( int64_t iCallerRunId, double dMinScore )
        {
            return xQuerySizeSpecific.scalar( iCallerRunId, iCallerRunId, dMinScore );
        } // method

        inline void updateCoverage( SvCall& rCall )
        {
            xSetCoverageForCall.bindAndExecute( (uint32_t)rCall.uiCoverage, rCall.iId );
        } // method

        inline void deleteCall( int64_t iCallId )
        {
            xDeleteCall1.bindAndExecute( iCallId );
            xDeleteCall2.bindAndExecute( iCallId );
        } // method

        inline void deleteCall( SvCall& rCall )
        {
            deleteCall( rCall.iId );
        } // method

        inline int64_t insertCall( int64_t iSvCallerRunId, SvCall& rCall )
        {
            int64_t iCallId = this->xInsertRow(
                iSvCallerRunId, (uint32_t)rCall.uiFromStart, (uint32_t)rCall.uiToStart, (uint32_t)rCall.uiFromSize,
                (uint32_t)rCall.uiToSize, rCall.bSwitchStrand,
                // NucSeqSql can deal with nullpointers
                NucSeqSql( rCall.pInsertedSequence ), (uint32_t)rCall.uiNumSuppNt, (uint32_t)rCall.uiCoverage, -1 );
            rCall.iId = iCallId;
            xInsertRTree( iCallId, iSvCallerRunId, iSvCallerRunId, (uint32_t)rCall.uiFromStart,
                          (uint32_t)rCall.uiFromStart + (uint32_t)rCall.uiFromSize, (uint32_t)rCall.uiToStart,
                          (uint32_t)rCall.uiToStart + (uint32_t)rCall.uiToSize );
            return iCallId;
        } // method

        inline int64_t updateCall( int64_t iSvCallerRunId, SvCall& rCall )
        {
            xUpdateCall.bindAndExecute( (uint32_t)rCall.uiFromStart, (uint32_t)rCall.uiToStart,
                                        (uint32_t)rCall.uiFromSize, (uint32_t)rCall.uiToSize, rCall.bSwitchStrand,
                                        // NucSeqSql can deal with nullpointers
                                        NucSeqSql( rCall.pInsertedSequence ), (uint32_t)rCall.uiNumSuppNt,
                                        (uint32_t)rCall.uiCoverage, rCall.iId );
            xUpdateRTree.bindAndExecute( iSvCallerRunId, iSvCallerRunId, (uint32_t)rCall.uiFromStart,
                                         (uint32_t)rCall.uiFromStart + (uint32_t)rCall.uiFromSize,
                                         (uint32_t)rCall.uiToStart,
                                         (uint32_t)rCall.uiToStart + (uint32_t)rCall.uiToSize, rCall.iId );
            return rCall.iId;
        } // method

        inline int64_t callArea( int64_t iCallerRunId, double dMinScore )
        {
            return xCallArea.scalar( iCallerRunId, iCallerRunId, dMinScore );
        } // method

        inline double maxScore( int64_t iCallerRunId )
        {
            return xMaxScore.scalar( iCallerRunId, iCallerRunId );
        } // method

        inline double minScore( int64_t iCallerRunId )
        {
            return xMinScore.scalar( iCallerRunId, iCallerRunId );
        } // method

        /**
         * returns how many calls of run A are overlapped by a call in run B
         */
        inline uint32_t numOverlaps( int64_t iCallerRunIdA, int64_t iCallerRunIdB, double dMinScore,
                                     int64_t iAllowedDist )
        {
            // uint32_t uiNumCalls = numCalls( iCallerRunIdA, 0 ) * 3;
            uint32_t uiRet = 0;
            auto vResults = xNumOverlaps.executeAndStoreAllInVector( iCallerRunIdB, dMinScore );
            for( auto xTup : vResults )
            {
                int64_t iId = std::get<0>( xTup );
                double dScore = std::get<1>( xTup );
                uint32_t uiFromStart = std::get<2>( xTup );
                uint32_t uiFromSize = std::get<3>( xTup );
                uint32_t uiToStart = std::get<4>( xTup );
                uint32_t uiToSize = std::get<5>( xTup );
                bool bSwitchStrand = std::get<6>( xTup );

                if( xNumOverlapsHelper1
                        .vExecuteAndReturnIterator( iCallerRunIdA, iCallerRunIdA, uiFromStart - iAllowedDist,
                                                    uiFromStart + uiFromSize + iAllowedDist, uiToStart - iAllowedDist,
                                                    uiToStart + uiToSize + iAllowedDist, bSwitchStrand )
                        .eof( ) )
                    continue;
                if( !xNumOverlapsHelper2
                         .vExecuteAndReturnIterator( iId, dScore, iCallerRunIdB, iCallerRunIdB,
                                                     uiFromStart - iAllowedDist,
                                                     uiFromStart + uiFromSize + iAllowedDist, uiToStart - iAllowedDist,
                                                     uiToStart + uiToSize + iAllowedDist, bSwitchStrand )
                         .eof( ) )
                    continue;
                uiRet += 1;
            } // for

            return uiRet;
        } // method

        /**
         * returns the average distance of class from the overlapped (due to fuzziness) SV
         */
        inline double blurOnOverlaps( int64_t iCallerRunIdA, int64_t iCallerRunIdB, double dMinScore,
                                      int64_t iAllowedDist )
        {
            int64_t uiSum = 0;
            int64_t uiCount = 0;
            for( int64_t iI = 0; iI <= iAllowedDist; iI++ )
            {
                uint32_t uiAmount = numOverlaps( iCallerRunIdA, iCallerRunIdB, dMinScore, iI );
                uiSum += uiAmount * iI;
                uiCount += uiAmount;
            } // for
            return uiSum / (double)uiCount;
        } // method

        /**
         * returns how many calls are invalid because they overlap another call with higher score
         */
        inline uint32_t numInvalidCalls( int64_t iCallerRunIdA, double dMinScore, int64_t iAllowedDist )
        {
            uint32_t uiRet = 0;
            xNumOverlaps.vExecuteAndForAllRowsUnpackedDo(
                [&]( int64_t iId, double dScore, uint32_t uiFromStart, uint32_t uiFromSize, uint32_t uiToStart,
                     uint32_t uiToSize, bool bSwitchStrand ) {
                    if( xNumOverlapsHelper2
                            .vExecuteAndReturnIterator( iCallerRunIdA, dScore, uiFromStart - iAllowedDist,
                                                        uiFromStart + uiFromSize + iAllowedDist,
                                                        uiToStart - iAllowedDist, uiToStart + uiToSize + iAllowedDist,
                                                        bSwitchStrand )
                            .eof( ) )
                        return;
                    uiRet += 1;
                },
                iCallerRunIdA, dMinScore );

            return uiRet;
        } // method

        // returns call id, jump start pos, next context, next from position, jump end position
        inline std::tuple<int64_t, uint32_t, bool, NucSeqSql, uint32_t> getNextCall( int64_t iCallerRun, //
                                                                                     uint32_t uiFrom, //
                                                                                     bool bForwardContext )
        {
            std::tuple<int64_t, uint32_t, bool, NucSeqSql, uint32_t> xRet;
            std::get<0>( xRet ) = -1;
            std::get<2>( xRet ) = bForwardContext; // does nothing...
            if( bForwardContext )
            {
                auto itQ = xNextCallForwardContext.vExecuteAndReturnIterator( iCallerRun, uiFrom );
                if( !itQ.eof( ) )
                {
                    auto xQ = itQ.get( );
                    std::get<0>( xRet ) = std::get<0>( xQ );
                    std::get<1>( xRet ) = std::get<5>( xQ );
                    std::get<2>( xRet ) = !std::get<1>( xQ );
                    std::get<3>( xRet ) = std::get<4>( xQ );
                    std::get<4>( xRet ) = std::get<1>( xQ ) ? std::get<2>( xQ ) + std::get<3>( xQ ) : std::get<2>( xQ );
                } // if
            } // if
            else
            {
                auto itQ = xNextCallBackwardContext.vExecuteAndReturnIterator( iCallerRun, uiFrom );
                if( !itQ.eof( ) )
                {
                    auto xQ = itQ.get( );
                    std::get<0>( xRet ) = std::get<0>( xQ );
                    std::get<1>( xRet ) = std::get<5>( xQ );
                    std::get<2>( xRet ) = std::get<1>( xQ );
                    std::get<3>( xRet ) = std::get<4>( xQ );
                    std::get<4>( xRet ) =
                        !std::get<1>( xQ ) ? std::get<2>( xQ ) + std::get<3>( xQ ) : std::get<2>( xQ );
                } // if
            } // else
            return xRet;
        } // method

        inline std::shared_ptr<Pack> reconstructSequencedGenome( std::shared_ptr<Pack> pRef, int64_t iCallerRun )
        {
            {
                CppSQLiteExtStatement( *pDatabase,
                                       ( "CREATE INDEX IF NOT EXISTS tmp_reconstruct_seq_index_1_" +
                                         std::to_string( iCallerRun ) +
                                         " ON sv_call_table (from_pos, id, switch_strand, to_pos, to_size, "
                                         "                  inserted_sequence, from_pos + from_size) "
                                         "WHERE sv_caller_run_id == " +
                                         std::to_string( iCallerRun ) )
                                           .c_str( ) )
                    .execDML( );
                CppSQLiteExtStatement( *pDatabase,
                                       ( "CREATE INDEX IF NOT EXISTS tmp_reconstruct_seq_index_2_" +
                                         std::to_string( iCallerRun ) +
                                         " ON sv_call_table (to_pos, id, switch_strand, from_pos, from_size, "
                                         "                  inserted_sequence) "
                                         "WHERE sv_caller_run_id == " +
                                         std::to_string( iCallerRun ) )
                                           .c_str( ) )
                    .execDML( );
            } // scope for CppSQLiteExtStatement

            // @todo at the moment this does not deal with jumped over sequences
            // @todo at the moment this does not check the regex (?)
            auto pRet = std::make_shared<Pack>( );

            std::set<int64_t> xVisitedCalls;

            NucSeq xCurrChrom;
            uint32_t uiCurrPos = 0;
            uint32_t uiContigCnt = 1;
            bool bForwContext = true;
            while( true )
            {
                // get the next call
                std::tuple<int64_t, uint32_t, bool, NucSeqSql, uint32_t> tNextCall;
                uint32_t uiIntermediatePos = uiCurrPos;
                do
                {
                    // search for the next call that we have not visited yet...
                    metaMeasureAndLogDuration<false>( "SQL", [&]( ) {
                        tNextCall = this->getNextCall( iCallerRun, uiIntermediatePos, bForwContext );
                    } );
                    if( std::get<0>( tNextCall ) == -1 || // there is no next call
                                                          // we have not visited the next call
                        xVisitedCalls.find( std::get<0>( tNextCall ) ) == xVisitedCalls.end( ) )
                        break;
                    // we have visited the next call and need to search again

                    // @todo this is extremely inefficient (if we have cylces in our graph wich we do not at the moment)
                    uiIntermediatePos += bForwContext ? 1 : -1;
                } while( true );
#if 0
                std::cout << "id: " << std::get<0>( tNextCall ) << " from: " << std::get<1>( tNextCall )
                          << " to: " << std::get<4>( tNextCall )
                          << ( std::get<2>( tNextCall ) ? " forward" : " rev-comp" ) << std::endl;
#endif
                if( std::get<0>( tNextCall ) == -1 ) // if there are no more calls
                {
                    metaMeasureAndLogDuration<false>( "seq copy final", [&]( ) {
                        pRef->vExtractContext( uiCurrPos, xCurrChrom, true, bForwContext );
                        pRet->vAppendSequence( "unnamed_contig_" + std::to_string( uiContigCnt++ ),
                                               "no_description_given", xCurrChrom );
                        xCurrChrom.vClear( );
                        /*
                         * for this we make use of the id system of contigs.
                         * the n forwards contigs have the ids: x*2 | 0 <= x <= n
                         * the n reverse complement contigs have the ids: x*2+1 | 0 <= x <= n
                         */
                        for( int64_t uiI = pRef->uiSequenceIdForPositionOrRev( uiCurrPos ) + ( bForwContext ? 2 : -1 );
                             uiI < (int64_t)pRef->uiNumContigs( ) * 2 && uiI >= 0;
                             uiI += ( bForwContext ? 2 : -2 ) )
                        {
                            pRef->vExtractContig( uiI, xCurrChrom, true );
                            pRet->vAppendSequence( "unnamed_contig_" + std::to_string( uiContigCnt++ ),
                                                   "no_description_given", xCurrChrom );
                            xCurrChrom.vClear( );
                        } // for
                    } );
                    break;
                } // if

                // we reach this point if there are more calls, so tNextCall is set properly here
                metaMeasureAndLogDuration<false>( "seq copy", [&]( ) {
                    // if the next call is in a different chromosome
                    while( pRef->bridgingPositions( uiCurrPos, std::get<1>( tNextCall ) ) )
                    {
                        // extract the remaining chromosome into xCurrChrom
                        uiCurrPos = (uint32_t)pRef->vExtractContext( uiCurrPos, xCurrChrom, true, bForwContext );
                        // append xCurrChrom to the pack
                        pRet->vAppendSequence( "unnamed_contig_" + std::to_string( uiContigCnt++ ),
                                               "no_description_given", xCurrChrom );
                        // clear xCurrChrom
                        xCurrChrom.vClear( );
                        // if the next call is several chromosomes over this loops keeps going
                    } // if
                    // the call is in the current chromosome / we have appended all skipped chromosomes
                    if( bForwContext )
                        pRef->vExtractSubsectionN( uiCurrPos, std::get<1>( tNextCall ), xCurrChrom, true );
                    else
                        pRef->vExtractSubsectionN( pRef->uiPositionToReverseStrand( uiCurrPos ) + 1,
                                                   pRef->uiPositionToReverseStrand( std::get<1>( tNextCall ) ) + 1, //
                                                   xCurrChrom,
                                                   true );
                    // append the skipped over sequence
                    if( std::get<3>( tNextCall ).pNucSeq != nullptr )
                        xCurrChrom.vAppend( std::get<3>( tNextCall ).pNucSeq->pxSequenceRef,
                                            std::get<3>( tNextCall ).pNucSeq->length( ) );

                    metaMeasureAndLogDuration<false>( "xInsertRow", [&]( ) {
                        // remember that we used this call
                        // pReconstructionTable->xInsertRow( std::get<0>( tNextCall ) );
                        xVisitedCalls.insert( std::get<0>( tNextCall ) );
                        bForwContext = std::get<2>( tNextCall );
                        uiCurrPos = std::get<4>( tNextCall );
                    } );
                } );
            } // while

            // clear up the temp table
            // pReconstructionTable->clearTable( );
            // @todo hmm eventually the extra indices should be cleared up?
            //       however, these do only exist for call sets where we reconstruct the genome
            //       i.e. the ground truth data set
            // CppSQLiteExtStatement( *pDatabase, "DROP INDEX tmp_reconstruct_seq_index_1 " ).execDML( );
            // CppSQLiteExtStatement( *pDatabase, "DROP INDEX tmp_reconstruct_seq_index_2 " ).execDML( );

            return pRet;
        } // method
    }; // class

    typedef CppSQLiteExtTable<int64_t, // call_id (foreign key)
                              int64_t // jump_id (foreign key)
                              >
        TP_SV_CALL_SUPPORT_TABLE;
    class SvCallSupportTable : public TP_SV_CALL_SUPPORT_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;
        CppSQLiteExtQueryStatement<int64_t> xDeleteRun;
        CppSQLiteExtStatement xDeleteCall;

      public:
        SvCallSupportTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_CALL_SUPPORT_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_call_support_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"call_id", "jump_id"},
                  false,
                  // constraints for table
                  std::vector<std::string>{"FOREIGN KEY (call_id) REFERENCES sv_call_table(id) ON DELETE CASCADE",
                                           "FOREIGN KEY (jump_id) REFERENCES sv_jump_table(id) ON DELETE CASCADE"} ),
              pDatabase( pDatabase ),
              xDeleteRun( *pDatabase, "DELETE FROM sv_call_support_table WHERE call_id IN ( SELECT id FROM "
                                      "sv_call_table WHERE sv_caller_run_id IN ( SELECT id FROM "
                                      "sv_caller_run_table WHERE name == ?))" ),
              xDeleteCall( *pDatabase,
                           "DELETE FROM sv_call_support_table "
                           "WHERE call_id = ? " )
        {
            pDatabase->execDML( "CREATE INDEX IF NOT EXISTS sv_call_support_index ON sv_call_support_table "
                                "(call_id, jump_id)" );
        } // default constructor

        inline void deleteRun( std::string& rS )
        {
            xDeleteRun.bindAndExecQuery<>( rS );
        } // method

        inline void deleteCall( int64_t iCallId )
        {
            xDeleteCall.bindAndExecute( iCallId );
        } // method

        inline void deleteCall( SvCall& rCall )
        {
            deleteCall( rCall.iId );
        } // method
    }; // class


  public:
    const std::string sName;
    std::shared_ptr<std::mutex> pWriteLock;
    std::shared_ptr<CppSQLiteDBExtended> pDatabase;
    std::shared_ptr<SequencerTable> pSequencerTable;
    std::shared_ptr<ContigCovTable> pContigCovTable;
    std::shared_ptr<ReadTable> pReadTable;
    std::shared_ptr<PairedReadTable> pPairedReadTable;
    std::shared_ptr<NameDescTable> pSvJumpRunTable;
    std::shared_ptr<SvJumpTable> pSvJumpTable;
    std::shared_ptr<SvCallerRunTable> pSvCallerRunTable;
    std::shared_ptr<SvCallRegExTable> pSvCallRegExTable;
    std::shared_ptr<SvCallTable> pSvCallTable;
    std::shared_ptr<SvCallSupportTable> pSvCallSupportTable;

    /**
     * @brief open a new database connection; with shared metadata (table pointers are shared)
     */
    SV_DB( SV_DB& rOther )
        : sName( rOther.sName ),
          pWriteLock( rOther.pWriteLock ),
          pDatabase( std::make_shared<CppSQLiteDBExtended>( "", rOther.sName, eOPEN_DB ) ),
          pSequencerTable( rOther.pSequencerTable ),
          pContigCovTable( rOther.pContigCovTable ),
          pReadTable( rOther.pReadTable ),
          pPairedReadTable( rOther.pPairedReadTable ),
          pSvJumpRunTable( rOther.pSvJumpRunTable ),
          pSvJumpTable( rOther.pSvJumpTable ),
          pSvCallerRunTable( rOther.pSvCallerRunTable ),
          pSvCallRegExTable( rOther.pSvCallRegExTable ),
          pSvCallTable( rOther.pSvCallTable ),
          pSvCallSupportTable( rOther.pSvCallSupportTable )
    {
        this->setNumThreads( 32 ); // @todo do this via a parameter
        pDatabase->execDML( "PRAGMA journal_mode=WAL;" ); // use write ahead mode
        pDatabase->execDML( "PRAGMA busy_timeout=0;" ); // do not throw sqlite busy errors
        // https://stackoverflow.com/questions/1711631/improve-insert-per-second-performance-of-sqlite
        // pDatabase->execDML( "PRAGMA synchronous = OFF;" ); // insert performance
        // pDatabase->execDML( "PRAGMA journal_mode = MEMORY;" ); // insert performance
    } // constructor

    SV_DB( std::string sName, enumSQLite3DBOpenMode xMode )
        : sName( sName ),
          pWriteLock( std::make_shared<std::mutex>( ) ),
          pDatabase( std::make_shared<CppSQLiteDBExtended>( "", sName, xMode ) ),
          pSequencerTable( std::make_shared<SequencerTable>( pDatabase ) ),
          pContigCovTable( std::make_shared<ContigCovTable>( pDatabase ) ),
          pReadTable( std::make_shared<ReadTable>( pDatabase ) ),
          pPairedReadTable( std::make_shared<PairedReadTable>( pDatabase, pReadTable ) ),
          pSvJumpRunTable( std::make_shared<NameDescTable>( pDatabase, "sv_jump_run_table" ) ),
          pSvJumpTable( std::make_shared<SvJumpTable>( pDatabase ) ),
          pSvCallerRunTable( std::make_shared<SvCallerRunTable>( pDatabase ) ),
          pSvCallRegExTable( std::make_shared<SvCallRegExTable>( pDatabase ) ),
          pSvCallTable( std::make_shared<SvCallTable>( pDatabase ) ),
          pSvCallSupportTable( std::make_shared<SvCallSupportTable>( pDatabase ) )
    {
        this->setNumThreads( 32 ); // @todo do this via a parameter
        pDatabase->execDML( "PRAGMA journal_mode=WAL;" ); // use write ahead mode
        pDatabase->execDML( "PRAGMA busy_timeout=0;" ); // do not throw sqlite busy errors
        if( xMode == eCREATE_DB )
        {
            // https://stackoverflow.com/questions/1711631/improve-insert-per-second-performance-of-sqlite
            pDatabase->execDML( "PRAGMA synchronous = OFF;" ); // insert performance
            pDatabase->execDML( "PRAGMA journal_mode = MEMORY;" ); // insert performance
        }
    } // constructor

    SV_DB( std::string sName ) : SV_DB( sName, eCREATE_DB )
    {} // constructor

    SV_DB( std::string sName, std::string sMode ) : SV_DB( sName, sMode == "create" ? eCREATE_DB : eOPEN_DB )
    {} // constructor

    inline void createJumpIndices( int64_t uiRun )
    {
        pSvJumpTable->createIndices( uiRun );
    } // method


    inline void addScoreIndex( int64_t iCallerRunId )
    {
        pSvCallTable->addScoreIndex( iCallerRunId );
    } // method

    inline void setNumThreads( size_t uiN )
    {
        pDatabase->set_num_threads( (int)uiN );
    } // method

    inline int64_t getRunId( std::string& rS )
    {
        return pSvCallerRunTable->getId( rS );
    } // method

    inline int64_t getCallArea( int64_t iCallerRunId, double dMinScore )
    {
        return pSvCallTable->callArea( iCallerRunId, dMinScore );
    } // method

    inline double getMaxScore( int64_t iCallerRunId )
    {
        return pSvCallTable->maxScore( iCallerRunId );
    } // method

    inline double getMinScore( int64_t iCallerRunId )
    {
        return pSvCallTable->minScore( iCallerRunId );
    } // method

    inline uint32_t getNumOverlapsBetweenCalls( int64_t iCallerRunIdA, int64_t iCallerRunIdB, double dMinScore,
                                                int64_t iAllowedDist )
    {
        return pSvCallTable->numOverlaps( iCallerRunIdA, iCallerRunIdB, dMinScore, iAllowedDist );
    } // method

    inline double getBlurOnOverlapsBetweenCalls( int64_t iCallerRunIdA, int64_t iCallerRunIdB, double dMinScore,
                                                 int64_t iAllowedDist )
    {
        return pSvCallTable->blurOnOverlaps( iCallerRunIdA, iCallerRunIdB, dMinScore, iAllowedDist );
    } // method

    inline uint32_t getNumInvalidCalls( int64_t iCallerRunIdA, double dMinScore, int64_t iAllowedDist )
    {
        return pSvCallTable->numInvalidCalls( iCallerRunIdA, dMinScore, iAllowedDist );
    } // method

    inline uint32_t getNumCalls( int64_t iCallerRunId, double dMinScore )
    {
        return pSvCallTable->numCalls( iCallerRunId, dMinScore );
    } // method

    inline uint32_t getNumRuns( )
    {
        return pSvCallerRunTable->size( );
    } // method

    inline std::vector<int64_t> getNumNts( int64_t iSequencerId )
    {
        return pContigCovTable->getNumNt( iSequencerId );
    } // method

    inline std::string getRunName( int64_t iId )
    {
        return pSvCallerRunTable->getName( iId );
    } // method

    inline std::string getRunDesc( int64_t iId )
    {
        return pSvCallerRunTable->getDesc( iId );
    } // method

    inline int64_t getRunJumpId( int64_t iId )
    {
        return pSvCallerRunTable->getSvJumpRunId( iId );
    } // method

    inline std::string getRunDate( int64_t iId )
    {
        return pSvCallerRunTable->getDate( iId );
    } // method

    inline bool runExists( int64_t iId )
    {
        return pSvCallerRunTable->exists( iId );
    } // method

    inline std::vector<int64_t> getNewestUniqueRuns( uint32_t uiNum, std::string sDesc )
    {
        return pSvCallerRunTable->getNewestUnique( uiNum, sDesc );
    } // method

    inline bool nameExists( std::string sName )
    {
        return pSvCallerRunTable->nameExists( sName );
    } // method

    inline int64_t insertSvCallerRun( std::string rsSvCallerName, std::string rsSvCallerDesc, int64_t uiJumpRunId )
    {
        return pSvCallerRunTable->insert( rsSvCallerName, rsSvCallerDesc, uiJumpRunId );
    }

    inline int64_t insertSvJumpRun( std::string rsSvCallerName, std::string rsSvCallerDesc )
    {
        return pSvJumpRunTable->insert( rsSvCallerName, rsSvCallerDesc );
    }

    inline std::shared_ptr<Pack> reconstructSequencedGenome( std::shared_ptr<Pack> pRef, int64_t iCallerRun )
    {
        return pSvCallTable->reconstructSequencedGenome( pRef, iCallerRun );
    } // method

    inline void updateCoverage( SvCall& rCall )
    {
        pSvCallTable->updateCoverage( rCall );
    } // method

    inline std::shared_ptr<NucSeq> getRead( int64_t iId )
    {
        return pReadTable->getRead( iId );
    } // method

    class ReadInserter
    {
      private:
        // this is here so that it gets destructed after the transaction context
        std::shared_ptr<SV_DB> pDB;
        // must be after the DB so that it is deconstructed first
        CppSQLiteExtImmediateTransactionContext xTransactionContext;

      public:
        int64_t uiSequencerId;

        ReadInserter( std::shared_ptr<SV_DB> pDB, std::string sSequencerName, std::shared_ptr<Pack> pPack )
            : pDB( pDB ),
              xTransactionContext( *pDB->pDatabase ),
              uiSequencerId( pDB->pSequencerTable->insertSequencer( sSequencerName ) )
        {
            pDB->pContigCovTable->insert( uiSequencerId, pPack );
        } // constructor

        ReadInserter( const ReadInserter& rOther ) = delete; // delete copy constructor

        inline void insertRead( std::shared_ptr<NucSeq> pRead )
        {
            pDB->pReadTable->insertRead( uiSequencerId, pRead );
        } // method

        inline void insertPairedRead( std::shared_ptr<NucSeq> pReadA, std::shared_ptr<NucSeq> pReadB )
        {
            pDB->pPairedReadTable->insertRead( uiSequencerId, pReadA, pReadB );
        } // method

        inline void insertFastaFiles( const ParameterSetManager& rParameters, const std::vector<fs::path>& vsFileNames )
        {
            FileListReader xReader( rParameters, vsFileNames );
            {
                ThreadPool xPool( 4 );
                std::mutex xReadLock, xWriteLock;

                xPool.enqueue(
                    []( size_t uiTid, FileListReader* pReader, ReadInserter* pInserter, std::mutex* pReadLock,
                        std::mutex* pWriteLock ) {
                        while( !pReader->isFinished( ) )
                        {
                            std::shared_ptr<NucSeq> pRead;
                            {
                                std::lock_guard<std::mutex> xGuard( *pReadLock );
                                pRead = pReader->execute( );
                            } // scope for xGuard
                            {
                                std::lock_guard<std::mutex> xGuard( *pWriteLock );
                                pInserter->insertRead( pRead );
                            } // scope for xGuard
                        } // while
                        return 0;
                    },
                    &xReader, this, &xReadLock, &xWriteLock );
            } // scope for thread pool
        } // method

        inline void insertPairedFastaFiles( const ParameterSetManager& rParameters,
                                            const std::vector<fs::path>& vsFileNames1,
                                            const std::vector<fs::path>& vsFileNames2 )
        {
            PairedFileReader xReader( rParameters, vsFileNames1, vsFileNames2 );
            {
                ThreadPool xPool( 4 );
                std::mutex xReadLock, xWriteLock;

                xPool.enqueue(
                    []( size_t uiTid, PairedFileReader* pReader, ReadInserter* pInserter, std::mutex* pReadLock,
                        std::mutex* pWriteLock ) {
                        while( !pReader->isFinished( ) )
                        {
                            std::shared_ptr<TP_PAIRED_READS> pvReads;
                            {
                                std::lock_guard<std::mutex> xGuard( *pReadLock );
                                pvReads = pReader->execute( );
                            } // scope for xGuard
                            {
                                std::lock_guard<std::mutex> xGuard( *pWriteLock );
                                pInserter->insertPairedRead( ( *pvReads )[ 0 ], ( *pvReads )[ 1 ] );
                            } // scope for xGuard
                        } // while
                        return 0;
                    },
                    &xReader, this, &xReadLock, &xWriteLock );
            } // scope for thread pool
        } // method
    }; // class

    class SvJumpInserter
    {
        // this is here so that it gets destructed after the transaction context
        std::shared_ptr<SV_DB> pDB;
        // must be after the DB so that it is deconstructed first
        CppSQLiteExtImmediateTransactionContext xTransactionContext;

      public:
        const int64_t iSvJumpRunId;

        class ReadContex
        {
          private:
            std::shared_ptr<SvJumpTable> pSvJumpTable;
            const int64_t iSvJumpRunId;
            const int64_t iReadId;

          public:
            ReadContex( std::shared_ptr<SvJumpTable> pSvJumpTable, const int64_t iSvJumpRunId, const int64_t iReadId )
                : pSvJumpTable( pSvJumpTable ), iSvJumpRunId( iSvJumpRunId ), iReadId( iReadId )
            {} // constructor

            inline void insertJump( SvJump& rJump )
            {
                // make sure the read id matches the read context
                if( rJump.iReadId == -1 ) // if there is no read id given yet add it
                    rJump.iReadId = iReadId;
                else // otherwise assert it matches
                    assert( rJump.iReadId == iReadId );

                if( rJump.does_switch_strand( ) )
                    assert( rJump.from_start( ) >= std::numeric_limits<int64_t>::max( ) / 2 );
                rJump.iId = pSvJumpTable->xInsertRow(
                    iSvJumpRunId, rJump.iReadId, rJump.from_start( ), rJump.from_end( ), (uint32_t)rJump.uiFrom,
                    (uint32_t)rJump.uiTo, (uint32_t)rJump.uiQueryFrom, (uint32_t)rJump.uiQueryTo,
                    (uint32_t)rJump.uiNumSupportingNt, rJump.bFromForward, rJump.bToForward, rJump.bFromSeedStart );
            } // method
        }; // class

        SvJumpInserter( std::shared_ptr<SV_DB> pDB, int64_t iSvJumpRunId )
            : pDB( pDB ), xTransactionContext( *pDB->pDatabase ), iSvJumpRunId( iSvJumpRunId )
        {} // constructor

        SvJumpInserter( std::shared_ptr<SV_DB> pDB,
                        const std::string& rsSvCallerName,
                        const std::string& rsSvCallerDesc )
            : pDB( pDB ),
              xTransactionContext( *pDB->pDatabase ),
              iSvJumpRunId( pDB->pSvJumpRunTable->insert( rsSvCallerName, rsSvCallerDesc ) )
        {} // constructor

        inline ReadContex readContext( int64_t iReadId )
        {
            return ReadContex( pDB->pSvJumpTable, iSvJumpRunId, iReadId );
        } // method

    }; // class

    class SvCallInserter
    {
        // this is here so that it gets destructed after the transaction context
        std::shared_ptr<SV_DB> pDB;
        // must be after the DB so that it is deconstructed first
        CppSQLiteExtImmediateTransactionContext xTransactionContext;

      public:
        const int64_t iSvCallerRunId;

        class CallContex
        {
          private:
            std::shared_ptr<SvCallSupportTable> pSvCallSupportTable;
            const int64_t iCallId;

          public:
            CallContex( std::shared_ptr<SvCallSupportTable> pSvCallSupportTable, const int64_t iCallId )
                : pSvCallSupportTable( pSvCallSupportTable ), iCallId( iCallId )
            {} // constructor

            inline void addSupport( SvJump& rJump )
            {
                pSvCallSupportTable->xInsertRow( iCallId, rJump.iId );
            } // method

            inline void addSupport( int64_t iId )
            {
                pSvCallSupportTable->xInsertRow( iCallId, iId );
            } // method

            inline void remSupport( )
            {
                pSvCallSupportTable->deleteCall( iCallId );
            } // method
        }; // class

        SvCallInserter( std::shared_ptr<SV_DB> pDB, const int64_t iSvCallerRunId )
            : pDB( pDB ), xTransactionContext( *pDB->pDatabase ), iSvCallerRunId( iSvCallerRunId )
        {} // constructor

        SvCallInserter( std::shared_ptr<SV_DB> pDB,
                        const std::string& rsSvCallerName,
                        const std::string& rsSvCallerDesc,
                        const int64_t uiJumpRunId )
            : SvCallInserter( pDB, pDB->pSvCallerRunTable->insert( rsSvCallerName, rsSvCallerDesc, uiJumpRunId ) )
        {} // constructor

        SvCallInserter( const SvCallInserter& ) = delete; // delete copy constructor

        inline void insertCall( SvCall& rCall )
        {
            CallContex xContext( pDB->pSvCallSupportTable, pDB->pSvCallTable->insertCall( iSvCallerRunId, rCall ) );
            for( int64_t iId : rCall.vSupportingJumpIds )
                xContext.addSupport( iId );
        } // method

        inline void updateCall( SvCall& rCall )
        {
            CallContex xContext( pDB->pSvCallSupportTable, pDB->pSvCallTable->updateCall( iSvCallerRunId, rCall ) );
            // remove the link between jumps and this call
            xContext.remSupport( );
            // reinsert the link (no need to compare old and new set this way)
            for( int64_t iId : rCall.vSupportingJumpIds )
                xContext.addSupport( iId );
        } // method

    }; // class

    inline uint32_t numJumps( )
    {
        return pSvJumpTable->numJumps( );
    } // method

    inline uint32_t numCalls( )
    {
        return pSvCallTable->numCalls( );
    } // method

}; // class

// @todo does buffering in vector increase the speed here?
class SortedSvJumpFromSql
{
    const std::shared_ptr<Presetting> pSelectedSetting;
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<int64_t, uint32_t, uint32_t, uint32_t, uint32_t, bool, bool, bool, uint32_t, int64_t,
                               int64_t>
        xQueryStart;
    CppSQLiteExtQueryStatement<int64_t, uint32_t, uint32_t, uint32_t, uint32_t, bool, bool, bool, uint32_t, int64_t,
                               int64_t>
        xQueryEnd;
    CppSQLiteExtQueryStatement<int64_t, uint32_t, uint32_t, uint32_t, uint32_t, bool, bool, bool, uint32_t, int64_t,
                               int64_t>::Iterator xTableIteratorStart;
    CppSQLiteExtQueryStatement<int64_t, uint32_t, uint32_t, uint32_t, uint32_t, bool, bool, bool, uint32_t, int64_t,
                               int64_t>::Iterator xTableIteratorEnd;

  public:
    SortedSvJumpFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSvCallerRunId )
        : pSelectedSetting( rParameters.getSelected( ) ),
          pDb( pDb ),
          xQueryStart( *pDb->pDatabase,
                       "SELECT sort_pos_start, from_pos, to_pos, query_from, query_to, from_forward, to_forward, "
                       "       from_seed_start, num_supporting_nt, id, read_id "
                       "FROM sv_jump_table "
                       "WHERE sv_jump_run_id == ? "
                       "ORDER BY sort_pos_start" ),
          xQueryEnd( *pDb->pDatabase,
                     "SELECT sort_pos_end, from_pos, to_pos, query_from, query_to, from_forward, to_forward, "
                     "       from_seed_start, num_supporting_nt, id, read_id "
                     "FROM sv_jump_table "
                     "WHERE sv_jump_run_id == ? "
                     "ORDER BY sort_pos_end" ),
          xTableIteratorStart( xQueryStart.vExecuteAndReturnIterator( iSvCallerRunId ) ),
          xTableIteratorEnd( xQueryEnd.vExecuteAndReturnIterator( iSvCallerRunId ) )
    {} // constructor

    SortedSvJumpFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSvCallerRunId,
                         uint32_t uiX, uint32_t uiY, uint32_t uiW, uint32_t uiH )
        : pSelectedSetting( rParameters.getSelected( ) ),
          pDb( pDb ),
          xQueryStart( *pDb->pDatabase,
                       "SELECT sort_pos_start, from_pos, to_pos, query_from, query_to, from_forward, to_forward, "
                       "       from_seed_start, num_supporting_nt, id, read_id "
                       "FROM sv_jump_table "
                       "WHERE sv_jump_run_id == ? "
                       "AND ( (from_pos >= ? AND from_pos <= ?) OR from_pos == ? ) "
                       "AND ( (to_pos >= ? AND to_pos <= ?) OR to_pos == ? ) "
                       "ORDER BY sort_pos_start" ),
          xQueryEnd( *pDb->pDatabase,
                     "SELECT sort_pos_end, from_pos, to_pos, query_from, query_to, from_forward, to_forward, "
                     "       from_seed_start, num_supporting_nt, id, read_id "
                     "FROM sv_jump_table "
                     "WHERE sv_jump_run_id == ? "
                     "AND ( (from_pos >= ? AND from_pos <= ?) OR from_pos == ? ) "
                     "AND ( (to_pos >= ? AND to_pos <= ?) OR to_pos == ? ) "
                     "ORDER BY sort_pos_end" ),
          xTableIteratorStart( xQueryStart.vExecuteAndReturnIterator(
              iSvCallerRunId, uiX, uiX + uiW, std::numeric_limits<uint32_t>::max( ), uiY, uiY + uiH,
              std::numeric_limits<uint32_t>::max( ) ) ),
          xTableIteratorEnd( xQueryEnd.vExecuteAndReturnIterator( iSvCallerRunId, uiX, uiX + uiW,
                                                                  std::numeric_limits<uint32_t>::max( ), uiY, uiY + uiH,
                                                                  std::numeric_limits<uint32_t>::max( ) ) )
    {} // constructor

    SortedSvJumpFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSvCallerRunId,
                         int64_t iS, int64_t iE )
        : pSelectedSetting( rParameters.getSelected( ) ),
          pDb( pDb ),
          xQueryStart( *pDb->pDatabase,
                       "SELECT sort_pos_start, from_pos, to_pos, query_from, query_to, from_forward, to_forward, "
                       "       from_seed_start, num_supporting_nt, id, read_id "
                       "FROM sv_jump_table "
                       "WHERE sv_jump_run_id == ? "
                       "AND sort_pos_start >= ? "
                       "AND sort_pos_start <= ? "
                       "ORDER BY sort_pos_start" ),
          xQueryEnd( *pDb->pDatabase,
                     "SELECT sort_pos_end, from_pos, to_pos, query_from, query_to, from_forward, to_forward, "
                     "      from_seed_start, num_supporting_nt, id, read_id "
                     "FROM sv_jump_table "
                     "WHERE sv_jump_run_id == ? "
                     "AND sort_pos_end >= ? "
                     "AND sort_pos_end <= ? "
                     "ORDER BY sort_pos_end" ),
          xTableIteratorStart( xQueryStart.vExecuteAndReturnIterator( iSvCallerRunId, iS, iE ) ),
          xTableIteratorEnd( xQueryEnd.vExecuteAndReturnIterator( iSvCallerRunId, iS, iE ) )
    {
        assert( iE >= iS );
#if DEBUG_LEVEL > 0
#if 0
        std::cout << "SortedSvJumpFromSql::xQueryStart" << std::endl;
        xQueryStart.bindAndExplain( iSvCallerRunId, iX, iX + iW );
        std::cout << "SortedSvJumpFromSql::xQueryEnd" << std::endl;
        xQueryEnd.bindAndExplain( iSvCallerRunId, iX, iX + iW );
#endif
#endif
    } // constructor

    bool hasNextStart( )
    {
        return !xTableIteratorStart.eof( );
    } // method

    bool hasNextEnd( )
    {
        return !xTableIteratorEnd.eof( );
    } // method

    bool nextStartIsSmaller( )
    {
        if( !hasNextStart( ) )
            return false;
        if( !hasNextEnd( ) )
            return true;
        auto xStartTup = xTableIteratorStart.get( );
        auto xEndTup = xTableIteratorEnd.get( );
        return std::get<0>( xStartTup ) <= std::get<0>( xEndTup );
    } // method

    std::shared_ptr<SvJump> getNextStart( )
    {
        assert( hasNextStart( ) );

        auto xTup = xTableIteratorStart.get( );
        xTableIteratorStart.next( );
        return std::make_shared<SvJump>( pSelectedSetting, std::get<1>( xTup ), std::get<2>( xTup ),
                                         std::get<3>( xTup ), std::get<4>( xTup ), std::get<5>( xTup ),
                                         std::get<6>( xTup ), std::get<7>( xTup ), std::get<8>( xTup ),
                                         std::get<9>( xTup ), std::get<10>( xTup ) );
    } // method

    std::shared_ptr<SvJump> getNextEnd( )
    {
        assert( hasNextEnd( ) );

        auto xTup = xTableIteratorEnd.get( );
        xTableIteratorEnd.next( );
        return std::make_shared<SvJump>( pSelectedSetting, std::get<1>( xTup ), std::get<2>( xTup ),
                                         std::get<3>( xTup ), std::get<4>( xTup ), std::get<5>( xTup ),
                                         std::get<6>( xTup ), std::get<7>( xTup ), std::get<8>( xTup ),
                                         std::get<9>( xTup ), std::get<10>( xTup ) );
    } // method

}; // class

// @todo this does not make the process sufficiently parallel
class AllNucSeqFromSql : public Module<NucSeq, true>
{
    /// this wrapper is required so that the iterator is never copied
    class InteratorHolder
    {
      public:
        CppSQLiteExtQueryStatement<NucSeqSql, int64_t>::Iterator xIterator;

        InteratorHolder( CppSQLiteExtQueryStatement<NucSeqSql, int64_t>& xQuery, int64_t iSequencerId, uint32_t uiRes,
                         uint32_t uiModulo )
            : xIterator( xQuery.vExecuteAndReturnIterator( iSequencerId, uiModulo, uiRes ) )
        {} // constructor

        InteratorHolder( CppSQLiteExtQueryStatement<NucSeqSql, int64_t>& xQuery, int64_t iSequencerId )
            : xIterator( xQuery.vExecuteAndReturnIterator( iSequencerId ) )
        {} // constructor

        InteratorHolder( CppSQLiteExtQueryStatement<NucSeqSql, int64_t>& xQuery )
            : xIterator( xQuery.vExecuteAndReturnIterator( ) )
        {} // constructor
    }; // class
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<NucSeqSql, int64_t> xQuery;
    std::shared_ptr<InteratorHolder> pTableIterator;
    int64_t iSequencerId;
    uint32_t uiRes;
    uint32_t uiModulo;

  public:
    AllNucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb )
        : pDb( std::make_shared<SV_DB>( *pDb ) ),
          xQuery( *this->pDb->pDatabase,
                  "SELECT read_table.sequence, read_table.id "
                  "FROM read_table " ),
          iSequencerId( -1 ),
          uiRes( 0 ),
          uiModulo( 0 )
    {} // constructor

    AllNucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSequencerId,
                      size_t uiRes, size_t uiModulo )
        : pDb( std::make_shared<SV_DB>( *pDb ) ),
          xQuery( *this->pDb->pDatabase,
                  ( uiModulo != 1 ? "SELECT read_table.sequence, read_table.id "
                                    "FROM read_table "
                                    "WHERE sequencer_id == ? "
                                    "AND read_table.id % ? == ? "
                                  : "SELECT read_table.sequence, read_table.id "
                                    "FROM read_table "
                                    "WHERE sequencer_id == ? " ) ),
          iSequencerId( iSequencerId ),
          uiRes( (uint32_t)uiRes ),
          uiModulo( (uint32_t)uiModulo )
    {
#if DEBUG_LEVEL > 0
#if 0
        std::cout << "AllNucSeqFromSql::xQuery" << std::endl;
        xQuery.bindAndExplain( iSequencerId, uiModulo, uiRes );
#endif
#endif
    } // constructor

    std::shared_ptr<NucSeq> execute( )
    {
        if( pTableIterator == nullptr && iSequencerId != -1 && uiModulo != 1 )
            pTableIterator = std::make_unique<InteratorHolder>( xQuery, iSequencerId, uiRes, uiModulo );
        else if( pTableIterator == nullptr && iSequencerId != -1 )
            pTableIterator = std::make_unique<InteratorHolder>( xQuery, iSequencerId );
        else if( pTableIterator == nullptr )
            pTableIterator = std::make_unique<InteratorHolder>( xQuery );

        if( pTableIterator->xIterator.eof( ) )
            throw AnnotatedException( "No more NucSeq in NucSeqFromSql module" );

        auto xTup = pTableIterator->xIterator.get( );
        // std::get<0>( xTup ).pNucSeq->sName = std::to_string( std::get<1>( xTup ) );
        std::get<0>( xTup ).pNucSeq->iId = std::get<1>( xTup );
        pTableIterator->xIterator.next( );

        if( pTableIterator->xIterator.eof( ) )
            setFinished( );
        return std::get<0>( xTup ).pNucSeq;
    } // method
}; // class

class NucSeqFromSql : public Module<NucSeq, true>
{
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t> xQuery;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t>::Iterator xTableIterator;

  public:
    NucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSequencerId )
        : pDb( std::make_shared<SV_DB>( *pDb ) ),
          xQuery( *this->pDb->pDatabase,
                  "SELECT read_table.sequence, read_table.id "
                  "FROM read_table "
                  "WHERE read_table.id NOT IN ( "
                  "   SELECT paired_read_table.first_read FROM paired_read_table "
                  "   UNION "
                  "   SELECT paired_read_table.second_read FROM paired_read_table "
                  ") "
                  "AND sequencer_id = ? " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( iSequencerId ) )
    {
        if( xTableIterator.eof( ) )
            setFinished( );
    } // constructor

    NucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb )
        : pDb( std::make_shared<SV_DB>( *pDb ) ),
          xQuery( *this->pDb->pDatabase,
                  "SELECT read_table.sequence, read_table.id "
                  "FROM read_table "
                  "WHERE read_table.id NOT IN ( "
                  "   SELECT paired_read_table.first_read FROM paired_read_table "
                  "   UNION "
                  "   SELECT paired_read_table.second_read FROM paired_read_table "
                  ") " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( ) )
    {
        if( xTableIterator.eof( ) )
            setFinished( );
    } // constructor

    std::shared_ptr<NucSeq> execute( )
    {
        if( xTableIterator.eof( ) )
            throw AnnotatedException( "No more NucSeq in NucSeqFromSql module" );

        auto xTup = xTableIterator.get( );
        // std::get<0>( xTup ).pNucSeq->sName = std::to_string( std::get<1>( xTup ) );
        std::get<0>( xTup ).pNucSeq->iId = std::get<1>( xTup );
        xTableIterator.next( );

        if( xTableIterator.eof( ) )
            setFinished( );
        return std::get<0>( xTup ).pNucSeq;
    } // method

    // override
    bool requiresLock( ) const
    {
        return true;
    } // method
}; // class

class PairedNucSeqFromSql : public Module<ContainerVector<std::shared_ptr<NucSeq>>, true>
{
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<NucSeqSql, NucSeqSql, uint32_t, uint32_t> xQuery;
    CppSQLiteExtQueryStatement<NucSeqSql, NucSeqSql, uint32_t, uint32_t>::Iterator xTableIterator;
    const bool bRevCompMate;

  public:
    PairedNucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSequencerId )
        : pDb( std::make_shared<SV_DB>( *pDb ) ),
          xQuery( *this->pDb->pDatabase,
                  "SELECT A.sequence, B.sequence, A.id, B.id "
                  "FROM read_table A, read_table B "
                  "INNER JOIN paired_read_table "
                  "ON paired_read_table.first_read == A.id "
                  "AND paired_read_table.second_read == B.id "
                  "AND A.sequencer_id = ? " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( iSequencerId ) ),
          bRevCompMate( rParameters.getSelected( )->xRevCompPairedReadMates->get( ) )
    {
        if( xTableIterator.eof( ) )
            setFinished( );
    } // constructor

    PairedNucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb )
        : pDb( std::make_shared<SV_DB>( *pDb ) ),
          xQuery( *this->pDb->pDatabase,
                  "SELECT A.sequence, B.sequence, A.id, B.id "
                  "FROM read_table A, read_table B "
                  "INNER JOIN paired_read_table "
                  "ON paired_read_table.first_read == A.id "
                  "AND paired_read_table.second_read == B.id " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( ) ),
          bRevCompMate( rParameters.getSelected( )->xRevCompPairedReadMates->get( ) )
    {
        if( xTableIterator.eof( ) )
            setFinished( );
    } // constructor

    std::shared_ptr<ContainerVector<std::shared_ptr<NucSeq>>> execute( )
    {
        if( xTableIterator.eof( ) )
            throw AnnotatedException( "No more NucSeq in PairedNucSeqFromSql module" );

        auto pRet = std::make_shared<ContainerVector<std::shared_ptr<NucSeq>>>( );

        auto xTup = xTableIterator.get( );
        pRet->push_back( std::get<0>( xTup ).pNucSeq );
        pRet->back( )->iId = std::get<2>( xTup );
        pRet->push_back( std::get<1>( xTup ).pNucSeq );
        pRet->back( )->iId = std::get<3>( xTup );

        if( bRevCompMate )
        {
            pRet->back( )->vReverse( );
            pRet->back( )->vSwitchAllBasePairsToComplement( );
        } // if

        xTableIterator.next( );

        if( xTableIterator.eof( ) )
            setFinished( );
        return pRet;
    } // method

    // override
    bool requiresLock( ) const
    {
        return true;
    } // method
}; // class

class SvDbInserter : public Module<Container, false, ContainerVector<SvJump>, NucSeq>
{
    std::shared_ptr<SV_DB> pDb;

  public:
    // this creates a transaction
    SV_DB::SvJumpInserter xInserter;

    SvDbInserter( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, std::string sRunDesc )
        : pDb( pDb ), xInserter( this->pDb, "MA-SV", sRunDesc )
    {} // constructor

    std::shared_ptr<Container> execute( std::shared_ptr<ContainerVector<SvJump>> pJumps, std::shared_ptr<NucSeq> pRead )
    {
        std::lock_guard<std::mutex> xGuard( *pDb->pWriteLock );

        SV_DB::SvJumpInserter::ReadContex xReadContext = xInserter.readContext( pRead->iId );
        for( SvJump& rJump : *pJumps )
            xReadContext.insertJump( rJump ); // also updates the jump ids;

        return std::make_shared<Container>( );
        // end of score for xGuard
    } // method
}; // class

class BufferedSvDbInserter : public Module<Container, false, ContainerVector<SvJump>, NucSeq>
{
    std::shared_ptr<SV_DB> pDb;
    int64_t iSvJumpRunId;

  public:
    std::vector<std::pair<std::shared_ptr<ContainerVector<SvJump>>, int64_t>> vBuffer;
    // this creates a transaction

    BufferedSvDbInserter( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSvJumpRunId )
        : pDb( pDb ), iSvJumpRunId( iSvJumpRunId )
    {} // constructor

    inline void commit( )
    {
        if( vBuffer.size( ) == 0 )
            return;
        SV_DB::SvJumpInserter xInserter( pDb, iSvJumpRunId );
        std::lock_guard<std::mutex> xGuard( *pDb->pWriteLock );
        for( auto xPair : vBuffer )
        {
            SV_DB::SvJumpInserter::ReadContex xReadContext = xInserter.readContext( xPair.second );
            for( SvJump& rJump : *xPair.first )
                xReadContext.insertJump( rJump ); // also updates the jump ids;
        } // for
        vBuffer.clear( );
        // end of scope for lock guard
    } // method

    ~BufferedSvDbInserter( )
    {
        commit( );
    } // destructor

    std::shared_ptr<Container> execute( std::shared_ptr<ContainerVector<SvJump>> pJumps, std::shared_ptr<NucSeq> pRead )
    {
        vBuffer.emplace_back( pJumps, pRead->iId );
        return std::make_shared<Container>( );
    } // method
}; // class


class SvCallerRunsFromDb
{
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<int64_t, std::string, std::string> xQuery;
    CppSQLiteExtQueryStatement<int64_t, std::string, std::string>::Iterator xTableIterator;

  public:
    SvCallerRunsFromDb( std::shared_ptr<SV_DB> pDb )
        : pDb( pDb ),
          xQuery( *pDb->pDatabase,
                  "SELECT id, name, desc "
                  "FROM sv_caller_run_table " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( ) )
    {} // constructor

    int64_t id( )
    {
        return std::get<0>( xTableIterator.get( ) );
    } // method

    std::string name( )
    {
        return std::get<1>( xTableIterator.get( ) );
    } // method

    std::string desc( )
    {
        return std::get<2>( xTableIterator.get( ) );
    } // method

    void next( )
    {
        xTableIterator.next( );
    } // method

    bool eof( )
    {
        return xTableIterator.eof( );
    } // method
}; // class

class SvCallsFromDb
{
    const std::shared_ptr<Presetting> pSelectedSetting;
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<int64_t, uint32_t, uint32_t, uint32_t, uint32_t, bool, NucSeqSql, uint32_t, uint32_t>
        xQuery;
    CppSQLiteExtQueryStatement<uint32_t, uint32_t, uint32_t, uint32_t, bool, bool, bool, uint32_t, int64_t, int64_t>
        xQuerySupport;
    CppSQLiteExtQueryStatement<int64_t, uint32_t, uint32_t, uint32_t, uint32_t, bool, NucSeqSql, uint32_t,
                               uint32_t>::Iterator xTableIterator;

  public:
    SvCallsFromDb( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSvCallerId )
        : pSelectedSetting( rParameters.getSelected( ) ),
          pDb( pDb ),
          xQuery( *pDb->pDatabase,
                  "SELECT id, from_pos, to_pos, from_size, to_size, switch_strand, inserted_sequence, supporting_nt, "
                  "       coverage "
                  "FROM sv_call_table "
                  "WHERE sv_caller_run_id == ? " ),
          xQuerySupport( *pDb->pDatabase,
                         "SELECT from_pos, to_pos, query_from, query_to, from_forward, to_forward, from_seed_start, "
                         "num_supporting_nt, sv_jump_table.id, read_id "
                         "FROM sv_call_support_table "
                         "JOIN sv_jump_table ON sv_call_support_table.jump_id == sv_jump_table.id "
                         "WHERE sv_call_support_table.call_id == ? " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( iSvCallerId ) )
    {} // constructor

    SvCallsFromDb( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSvCallerId,
                   double dMinScore )
        : pSelectedSetting( rParameters.getSelected( ) ),
          pDb( pDb ),
          xQuery( *pDb->pDatabase,
                  "SELECT id, from_pos, to_pos, from_size, to_size, switch_strand, inserted_sequence, supporting_nt, "
                  "       coverage "
                  "FROM sv_call_table "
                  "WHERE sv_caller_run_id == ? "
                  "AND (supporting_nt*1.0)/coverage >= ? " ),
          xQuerySupport( *pDb->pDatabase,
                         "SELECT from_pos, to_pos, query_from, query_to, from_forward, to_forward, from_seed_start, "
                         "num_supporting_nt, sv_jump_table.id, read_id "
                         "FROM sv_call_support_table "
                         "JOIN sv_jump_table ON sv_call_support_table.jump_id == sv_jump_table.id "
                         "WHERE sv_call_support_table.call_id == ? " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( iSvCallerId, dMinScore ) )
    {} // constructor

    SvCallsFromDb( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb, int64_t iSvCallerId,
                   uint32_t uiX, uint32_t uiY, uint32_t uiW, uint32_t uiH )
        : pSelectedSetting( rParameters.getSelected( ) ),
          pDb( pDb ),
          xQuery( *pDb->pDatabase,
                  "SELECT id, from_pos, to_pos, from_size, to_size, switch_strand, inserted_sequence, supporting_nt, "
                  "       coverage "
                  "FROM sv_call_table "
                  "WHERE sv_caller_run_id == ? "
                  "AND from_pos + from_size >= ? "
                  "AND to_pos + to_size >= ? "
                  "AND from_pos <= ? "
                  "AND to_pos <= ? " ),
          xQuerySupport( *pDb->pDatabase,
                         "SELECT from_pos, to_pos, query_from, query_to, from_forward, to_forward, from_seed_start, "
                         "num_supporting_nt, sv_jump_table.id, read_id "
                         "FROM sv_call_support_table "
                         "JOIN sv_jump_table ON sv_call_support_table.jump_id == sv_jump_table.id "
                         "WHERE sv_call_support_table.call_id == ? " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( iSvCallerId, uiX, uiY, uiX + uiW, uiY + uiH ) )
    {} // constructor

    SvCall next( )
    {
        auto xTup = xTableIterator.get( );
        SvCall xRet( std::get<1>( xTup ), // uiFromStart
                     std::get<2>( xTup ), // uiToStart
                     std::get<3>( xTup ), // uiFromSize
                     std::get<4>( xTup ), // uiToSize
                     std::get<5>( xTup ), // bSwitchStrand
                     std::get<7>( xTup ) // num_supporting_nt
        );
        xRet.uiCoverage = std::get<8>( xTup );
        xRet.pInsertedSequence = std::get<6>( xTup ).pNucSeq;
        xRet.iId = std::get<0>( xTup );
        auto xSupportIterator( xQuerySupport.vExecuteAndReturnIterator( std::get<0>( xTup ) ) );
        while( !xSupportIterator.eof( ) )
        {
            auto xTup = xSupportIterator.get( );
            xRet.vSupportingJumpIds.push_back( std::get<7>( xTup ) );
            xRet.vSupportingJumps.push_back( std::make_shared<SvJump>(
                pSelectedSetting, std::get<0>( xTup ), std::get<1>( xTup ), std::get<2>( xTup ), std::get<3>( xTup ),
                std::get<4>( xTup ), std::get<5>( xTup ), std::get<6>( xTup ), std::get<7>( xTup ), std::get<8>( xTup ),
                std::get<9>( xTup ) ) );
            xSupportIterator.next( );
        } // while
        xTableIterator.next( );
        return xRet;
    } // method

    bool hasNext( )
    {
        return !xTableIterator.eof( );
    } // method
}; // class

}; // namespace libMA

#ifdef WITH_PYTHON
#ifdef WITH_BOOST
void exportSoCDbWriter( );
#else
void exportSoCDbWriter( py::module& rxPyModuleId );
#endif
#endif
