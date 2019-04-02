/**
 * @file svDb.h
 * @details
 * The database interface for the structural variant caller
 */

#include "container/container.h"
#include "container/nucSeq.h"
#include "container/soc.h"
#include "module/module.h"
#include "util/exception.h"
#include "util/sqlite3.h"

namespace libMA
{

class SV_DB : public CppSQLite3DB, public Container
{
  private:
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
        {} // default constructor

        ~SequencerTable( )
        {
            if( pDatabase->eDatabaseOpeningMode == eCREATE_DB )
                pDatabase->execDML( "CREATE INDEX sequencer_id_index ON sequencer_table (id)" );
        } // deconstructor

        inline int64_t insertSequencer( std::string& sSequencerName )
        {
            return xInsertRow( sSequencerName );
        } // method
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

        ReadTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_READ_TABLE( *pDatabase, // the database where the table resides
                             "read_table", // name of the table in the database
                             // column definitions of the table
                             std::vector<std::string>{"sequencer_id", "name", "sequence"},
                             // constraints for table
                             std::vector<std::string>{"UNIQUE (sequencer_id, name)", //
                                                      "FOREIGN KEY (sequencer_id) REFERENCES sequencer_table(id)"} ),
              pDatabase( pDatabase ),
              xGetReadId( *pDatabase, "SELECT id FROM read_table WHERE sequencer_id = ? AND name = ?" )
        {} // default constructor

        ~ReadTable( )
        {
            if( pDatabase->eDatabaseOpeningMode == eCREATE_DB )
                pDatabase->execDML( "CREATE INDEX read_id_index ON read_table (id)" );
        } // deconstructor

        inline int64_t insertRead( int64_t uiSequencerId, std::shared_ptr<NucSeq> pRead )
        {
            for( size_t i = 0; i < 5; i++ ) // at most do 5 tries
            {
                try
                {
                    return xInsertRow( uiSequencerId, pRead->sName, NucSeqSql( pRead ) );
                } // try
                catch( CppSQLite3Exception& xException )
                {
                    if( bDoDuplicateWarning )
                    {
                        std::cerr << "WARNING: " << xException.errorMessage( ) << std::endl;
                        std::cerr << "Does your data contain duplicate reads? Current read name: " << pRead->sName
                                  << std::endl;
                        std::cerr << "Changing read name to: " << pRead->sName + "_2" << std::endl;
                        std::cerr << "This warning is only displayed once" << std::endl;
                        bDoDuplicateWarning = false;
                    } // if
                    pRead->sName += "_2";
                } // catch
            } // for
            throw AnnotatedException( "Could not insert read after 5 tries" );
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

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // read id (foreign key)
                                                     uint32_t, // soc index
                                                     uint32_t, // soc start
                                                     uint32_t, // soc end
                                                     uint32_t // soc score
                                                     >
        TP_SOC_TABLE;
    class SoCTable : public TP_SOC_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SoCTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SOC_TABLE( *pDatabase, // the database where the table resides
                            "soc_table", // name of the table in the database
                            // column definitions of the table
                            std::vector<std::string>{"read_id", "soc_index", "soc_start", "soc_end", "soc_score"},
                            // constraints for table
                            std::vector<std::string>{"FOREIGN KEY (read_id) REFERENCES read_table(id)"} ),
              pDatabase( pDatabase )
        {} // default constructor

        ~SoCTable( )
        {
            if( pDatabase->eDatabaseOpeningMode == eCREATE_DB )
            {
                pDatabase->execDML( "CREATE INDEX soc_start_index ON soc_table (soc_start)" );
            } // if
        } // deconstructor
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<std::string, // name
                                                     std::string // desc
                                                     >
        TP_SV_CALLER_RUN_TABLE;
    class SvCallerRunTable : public TP_SV_CALLER_RUN_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SvCallerRunTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_CALLER_RUN_TABLE( *pDatabase, // the database where the table resides
                                      "sv_caller_run_table", // name of the table in the database
                                      // column definitions of the table
                                      std::vector<std::string>{"name", "desc"} ),
              pDatabase( pDatabase )
        {} // default constructor

        ~SvCallerRunTable( )
        {
            if( pDatabase->eDatabaseOpeningMode == eCREATE_DB )
                pDatabase->execDML( "CREATE INDEX sv_caller_run_table_id_index ON sv_caller_run_table (id)" );
        } // deconstructor
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sv_caller_run_id (foreign key)
                                                     uint32_t, // start
                                                     uint32_t, // end
                                                     std::string // desc
                                                     >
        TP_SV_LINE_TABLE;
    class SvLineTable : public TP_SV_LINE_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SvLineTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_LINE_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_line_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"sv_caller_run_id", "start", "end", "desc"},
                  // constraints for table
                  std::vector<std::string>{"FOREIGN KEY (sv_caller_run_id) REFERENCES sv_caller_run_table(id)"} ),
              pDatabase( pDatabase )
        {} // default constructor

        ~SvLineTable( )
        {
            if( pDatabase->eDatabaseOpeningMode == eCREATE_DB )
            {
                pDatabase->execDML( "CREATE INDEX sv_line_table_id_index ON sv_line_table (id)" );
                pDatabase->execDML( "CREATE INDEX sv_line_table_start_index ON sv_line_table (start)" );
            } // if
        } // deconstructor
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sv_line_id (foreign key)
                                                     bool, // pass
                                                     std::string // desc
                                                     >
        TP_SV_LINE_FILTER_TABLE;
    class SvLineFilterTable : public TP_SV_LINE_FILTER_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SvLineFilterTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_LINE_FILTER_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_line_filter_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"sv_line_id", "pass", "desc"},
                  // constraints for table
                  std::vector<std::string>{"FOREIGN KEY (sv_line_id) REFERENCES sv_line_table(id)"} ),
              pDatabase( pDatabase )
        {} // default constructor
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sv line id (foreign key)
                                                     int64_t, // read id (foreign key)
                                                     uint32_t, // read pos
                                                     bool // on forward strand
                                                     >
        TP_SV_LINE_SUPPORT_TABLE;
    class SvLineSupportTable : public TP_SV_LINE_SUPPORT_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SvLineSupportTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_LINE_SUPPORT_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_line_support_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"sv_line_id", "read_id", "read_pos", "on_forward_strand"},
                  // constraints for table
                  std::vector<std::string>{"FOREIGN KEY (sv_line_id) REFERENCES sv_line_table(id)",
                                           "FOREIGN KEY (read_id) REFERENCES read_table(id)"} ),
              pDatabase( pDatabase )
        {} // default constructor

        ~SvLineSupportTable( )
        {
            if( pDatabase->eDatabaseOpeningMode == eCREATE_DB )
            {
                pDatabase->execDML( "CREATE INDEX sv_line_support_table_id_index ON sv_line_support_table (read_id)" );
                pDatabase->execDML(
                    "CREATE INDEX sv_line_support_table_start_index ON sv_line_support_table (sv_line_id)" );
            } // if
        } // deconstructor
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // soc_line_from (foreign key)
                                                     int64_t, // soc_line_to (foreign key)
                                                     bool, // do_jump
                                                     bool, // switch strand
                                                     std::string // desc
                                                     >
        TP_SV_LINE_CONNECTOR_TABLE;
    class SvLineConnectorTable : public TP_SV_LINE_CONNECTOR_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SvLineConnectorTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_LINE_CONNECTOR_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_line_connector_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"soc_line_from", "soc_line_to", "do_jump", "switch_strand", "desc"},
                  // constraints for table
                  std::vector<std::string>{"FOREIGN KEY (soc_line_from) REFERENCES sv_line_table(id)",
                                           "FOREIGN KEY (soc_line_to) REFERENCES sv_line_table(id)",
                                           "UNIQUE (soc_line_from)"} ),
              pDatabase( pDatabase )
        {} // default constructor
    }; // class

    typedef CppSQLiteExtTableWithAutomaticPrimaryKey<int64_t, // sv_line_connector_id (foreign key)
                                                     bool, // pass
                                                     std::string // desc
                                                     >
        TP_SV_LINE_CONNECTOR_FILTER_TABLE;
    class SvLineConnectorFilterTable : public TP_SV_LINE_CONNECTOR_FILTER_TABLE
    {
        std::shared_ptr<CppSQLiteDBExtended> pDatabase;

      public:
        SvLineConnectorFilterTable( std::shared_ptr<CppSQLiteDBExtended> pDatabase )
            : TP_SV_LINE_CONNECTOR_FILTER_TABLE(
                  *pDatabase, // the database where the table resides
                  "sv_line_connector_filter_table", // name of the table in the database
                  // column definitions of the table
                  std::vector<std::string>{"sv_line_connector_id", "pass", "desc"},
                  // constraints for table
                  std::vector<std::string>{
                      "FOREIGN KEY (sv_line_connector_id) REFERENCES sv_line_connector_table(id)"} ),
              pDatabase( pDatabase )
        {} // default constructor
    }; // class


    std::shared_ptr<CppSQLiteDBExtended> pDatabase;
    std::shared_ptr<SequencerTable> pSequencerTable;
    std::shared_ptr<ReadTable> pReadTable;
    std::shared_ptr<PairedReadTable> pPairedReadTable;
    std::shared_ptr<SoCTable> pSocTable;
    std::shared_ptr<SvCallerRunTable> pSvCallerRunTable;
    std::shared_ptr<SvLineTable> pSvLineTable;
    std::shared_ptr<SvLineFilterTable> pSvLineFilterTable;
    std::shared_ptr<SvLineSupportTable> pSvLineSupportTable;
    std::shared_ptr<SvLineConnectorTable> pSvLineConnectorTable;
    std::shared_ptr<SvLineConnectorFilterTable> pSvLineConnectorFilterTable;

    friend class NucSeqFromSql;
    friend class AllNucSeqFromSql;
    friend class PairedNucSeqFromSql;
    friend class PairedReadTable;

  public:
    SV_DB( std::string sName, enumSQLite3DBOpenMode xMode )
        : pDatabase( std::make_shared<CppSQLiteDBExtended>( "", sName, xMode ) ),
          pSequencerTable( std::make_shared<SequencerTable>( pDatabase ) ),
          pReadTable( std::make_shared<ReadTable>( pDatabase ) ),
          pPairedReadTable( std::make_shared<PairedReadTable>( pDatabase, pReadTable ) ),
          pSocTable( std::make_shared<SoCTable>( pDatabase ) ),
          pSvCallerRunTable( std::make_shared<SvCallerRunTable>( pDatabase ) ),
          pSvLineTable( std::make_shared<SvLineTable>( pDatabase ) ),
          pSvLineFilterTable( std::make_shared<SvLineFilterTable>( pDatabase ) ),
          pSvLineSupportTable( std::make_shared<SvLineSupportTable>( pDatabase ) ),
          pSvLineConnectorTable( std::make_shared<SvLineConnectorTable>( pDatabase ) ),
          pSvLineConnectorFilterTable( std::make_shared<SvLineConnectorFilterTable>( pDatabase ) )
    {} // constructor

    SV_DB( std::string sName ) : SV_DB( sName, eCREATE_DB )
    {} // constructor

    SV_DB( std::string sName, std::string sMode ) : SV_DB( sName, sMode == "create" ? eCREATE_DB : eOPEN_DB )
    {} // constructor

    class ReadInserter
    {
      private:
        // this is here so that it gets destructed after the transaction context
        std::shared_ptr<SV_DB> pDB;
        // must be after the DB so that it is deconstructed first
        CppSQLiteExtImmediateTransactionContext xTransactionContext;

        int64_t uiSequencerId;

      public:
        ReadInserter( std::shared_ptr<SV_DB> pDB, std::string sSequencerName )
            : pDB( pDB ),
              xTransactionContext( *pDB->pDatabase ),
              uiSequencerId( pDB->pSequencerTable->insertSequencer( sSequencerName ) )
        {} // constructor

        ReadInserter( const ReadInserter& rOther ) = delete; // delete copy constructor

        inline void insertRead( std::shared_ptr<NucSeq> pRead )
        {
            pDB->pReadTable->insertRead( uiSequencerId, pRead );
        } // method

        inline void insertPairedRead( std::shared_ptr<NucSeq> pReadA, std::shared_ptr<NucSeq> pReadB )
        {
            pDB->pPairedReadTable->insertRead( uiSequencerId, pReadA, pReadB );
        } // method
    }; // class

    class SoCInserter
    {
      private:
        // this is here so that it gets destructed after the transaction context
        std::shared_ptr<SV_DB> pDB;
        // must be after the DB so that it is deconstructed first
        CppSQLiteExtImmediateTransactionContext xTransactionContext;

        class ReadContex
        {
          private:
            int64_t iReadId;
            std::shared_ptr<SoCTable> pSocTable;

          public:
            ReadContex( int64_t iReadId, std::shared_ptr<SoCTable> pSocTable )
                : iReadId( iReadId ), pSocTable( pSocTable )
            {} // constructor

            inline void operator( )( uint32_t uiIndex, uint32_t uiStart, uint32_t uiEnd, uint32_t uiScore )
            {
                pSocTable->xInsertRow( iReadId, uiIndex, uiStart, uiEnd, uiScore );
            } // method
        }; // class

      public:
        SoCInserter( std::shared_ptr<SV_DB> pDB ) : pDB( pDB ), xTransactionContext( *pDB->pDatabase )
        {} // constructor

        SoCInserter( const SoCInserter& rOther ) = delete; // delete copy constructor

        inline ReadContex getReadContext( std::shared_ptr<NucSeq> pRead )
        {
            assert( pRead->iId != -1 );
            return ReadContex( pRead->iId, pDB->pSocTable );
        } // method

        inline std::pair<ReadContex, ReadContex> getPairedReadContext( std::shared_ptr<NucSeq> pReadA,
                                                                       std::shared_ptr<NucSeq> pReadB )
        {
            assert( pReadA->iId != -1 );
            assert( pReadB->iId != -1 );
            return std::make_pair( ReadContex( pReadA->iId, pDB->pSocTable ),
                                   ReadContex( pReadB->iId, pDB->pSocTable ) );
        } // method
    }; // class

    class SvInserter
    {
        // this is here so that it gets destructed after the transaction context
        std::shared_ptr<SV_DB> pDB;
        // must be after the DB so that it is deconstructed first
        CppSQLiteExtImmediateTransactionContext xTransactionContext;

        int64_t uiSvCallerRunId;

      public:
        class LineContex
        {
          private:
            int64_t iLineId;
            std::shared_ptr<SvLineSupportTable> pSvLineSupp;

          public:
            LineContex( int64_t iLineId, std::shared_ptr<SvLineSupportTable> pSvLineSupp )
                : iLineId( iLineId ), pSvLineSupp( pSvLineSupp )
            {} // constructor

            inline void insertSupport( int64_t iReadId, uint32_t uiPos, bool bOnForwardStrand )
            {
                pSvLineSupp->xInsertRow( iLineId, iReadId, uiPos, bOnForwardStrand );
            } // method
        }; // class

        SvInserter( std::shared_ptr<SV_DB> pDB, const std::string& rsSvCallerName, const std::string& rsSvCallerDesc )
            : pDB( pDB ),
              xTransactionContext( *pDB->pDatabase ),
              uiSvCallerRunId( pDB->pSvCallerRunTable->xInsertRow( rsSvCallerName, rsSvCallerDesc ) )
        {} // constructor

        SvInserter( const SvInserter& ) = delete; // delete copy constructor

        inline LineContex insertSvLine( nucSeqIndex uiStart, nucSeqIndex uiEnd, const std::string& rsDesc )
        {
            assert( uiStart <= uiEnd );
            return LineContex(
                pDB->pSvLineTable->xInsertRow( uiSvCallerRunId, (uint32_t)uiStart, (uint32_t)uiEnd, rsDesc ),
                pDB->pSvLineSupportTable );
        } // method

#if 0
        inline void connectSvLines( int64_t iFrom, int64_t iTo, bool bJump, const std::string& rsDesc )
        {
            pDB->pSvLineConnectorTable->xInsertRow( iFrom, iTo, bJump, rsDesc );
        } // method
#endif
    }; // class

    inline void clearSocTable( )
    {
        pSocTable->clearTable( );
    } // method

    inline void clearCallsTable( )
    {
        pSvLineConnectorFilterTable->clearTable( );
        pSvLineConnectorTable->clearTable( );
        pSvLineSupportTable->clearTable( );
        pSvLineFilterTable->clearTable( );
        pSvLineTable->clearTable( );
        pSvCallerRunTable->clearTable( );
    } // method

    inline bool hasSoCs( )
    {
        return !pSocTable->empty( );
    } // method

}; // class

class SoCDbWriter : public Module<Container, false, NucSeq, SoCPriorityQueue>
{
  private:
    std::shared_ptr<SV_DB::SoCInserter> pInserter;
    std::shared_ptr<std::mutex> pMutex;
    size_t uiNumSoCsToRecord;

  public:
    SoCDbWriter( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB::SoCInserter> pInserter )
        : pInserter( pInserter ),
          pMutex( new std::mutex ),
          uiNumSoCsToRecord( rParameters.getSelected( )->xNSoCs->get( ) )
    {} // constructor

    std::shared_ptr<Container> execute( std::shared_ptr<NucSeq> pQuery, std::shared_ptr<SoCPriorityQueue> pSoCQueue )
    {
        std::lock_guard<std::mutex> xGuard( *pMutex );
        auto xInsertContext = pInserter->getReadContext( pQuery );
        for( uint32_t uiI = 0; uiI < uiNumSoCsToRecord; uiI++ )
        {
            if( pSoCQueue->empty( ) )
                break;

            auto xSoCTup = pSoCQueue->pop_info( );
            xInsertContext( uiI, (uint32_t)std::get<0>( xSoCTup ), (uint32_t)std::get<1>( xSoCTup ),
                            std::get<2>( xSoCTup ) );
        } // for

        return std::make_shared<Container>( );
    } // method
}; // class

class PairedSoCDbWriter : public Module<Container, false, NucSeq, NucSeq, SoCPriorityQueue, SoCPriorityQueue>
{
  private:
    std::shared_ptr<SV_DB::SoCInserter> pInserter;
    std::shared_ptr<std::mutex> pMutex;
    size_t uiNumSoCsToRecord = 1; // @todo expose

  public:
    PairedSoCDbWriter( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB::SoCInserter> pInserter )
        : pInserter( pInserter ), pMutex( new std::mutex )
    {} // constructor

    std::shared_ptr<Container> execute( std::shared_ptr<NucSeq> pQueryA,
                                        std::shared_ptr<SoCPriorityQueue>
                                            pSoCQueueA,
                                        std::shared_ptr<NucSeq>
                                            pQueryB,
                                        std::shared_ptr<SoCPriorityQueue>
                                            pSoCQueueB )
    {
        std::lock_guard<std::mutex> xGuard( *pMutex );
        auto txPairedInsertContext = pInserter->getPairedReadContext( pQueryA, pQueryB );
        for( uint32_t uiI = 0; uiI < uiNumSoCsToRecord; uiI++ )
        {
            if( pSoCQueueA->empty( ) )
                break;

            auto xSoCTup = pSoCQueueA->pop_info( );
            txPairedInsertContext.first( uiI, (uint32_t)std::get<0>( xSoCTup ), (uint32_t)std::get<1>( xSoCTup ),
                                         std::get<2>( xSoCTup ) );
        } // for
        for( uint32_t uiI = 0; uiI < uiNumSoCsToRecord; uiI++ )
        {
            if( pSoCQueueB->empty( ) )
                break;

            auto xSoCTup = pSoCQueueB->pop_info( );
            txPairedInsertContext.second( uiI, (uint32_t)std::get<0>( xSoCTup ), (uint32_t)std::get<1>( xSoCTup ),
                                          std::get<2>( xSoCTup ) );
        } // for

        return std::make_shared<Container>( );
    } // method
}; // class

class AllNucSeqFromSql : public Module<NucSeq, true>
{
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t> xQuery;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t>::Iterator xTableIterator;

  public:
    AllNucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb )
        : pDb( pDb ),
          xQuery( *pDb->pDatabase,
                  "SELECT read_table.sequence, read_table.id "
                  "FROM read_table " ),
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

class NucSeqFromSql : public Module<NucSeq, true>
{
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t> xQuery;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t>::Iterator xTableIterator;

  public:
    NucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb )
        : pDb( pDb ),
          xQuery( *pDb->pDatabase,
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

  public:
    PairedNucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb )
        : pDb( pDb ),
          xQuery( *pDb->pDatabase,
                  "SELECT A.sequence, B.sequence, A.id, B.id "
                  "FROM read_table A, read_table B "
                  "INNER JOIN paired_read_table "
                  "ON paired_read_table.first_read == A.id "
                  "AND paired_read_table.second_read == B.id " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( ) )
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

#if 0
class SortedSoCFromSql : public Module<ContainerVector<std::shared_ptr<NucSeq>, std::shared_ptr<Seeds>>, true>
{
    std::shared_ptr<SV_DB> pDb;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t, uint32_t> xQuery;
    CppSQLiteExtQueryStatement<NucSeqSql, uint32_t, uint32_t>::Iterator xTableIterator;

  public:
    PairedNucSeqFromSql( const ParameterSetManager& rParameters, std::shared_ptr<SV_DB> pDb )
        : pDb( pDb ),
          xQuery( *pDb->pDatabase,
                  "SELECT read_table.sequence, read_table.id, soc_table.id "
                  "FROM read_table "
                  "JOIN soc_table "
                  "ON soc_table.read_id == read_table.id "
                  "ORDER BY soc_table.soc_start " ),
          xTableIterator( xQuery.vExecuteAndReturnIterator( ) )
    {
        if( xTableIterator.eof( ) )
            setFinished( );
    } // constructor

    std::shared_ptr<ContainerVector<std::shared_ptr<NucSeq>, std::shared_ptr<Seeds>>> execute( )
    {
        if( xTableIterator.eof( ) )
            throw AnnotatedException( "No more NucSeq in PairedNucSeqFromSql module" );

        auto pRet = std::make_shared<ContainerVector<std::shared_ptr<NucSeq>>>( );

        auto xTup = xTableIterator.get( );
        pRet->push_back( std::get<0>( xTup ).pNucSeq );
        pRet->back( )->iId = std::get<2>( xTup );
        pRet->push_back( std::get<1>( xTup ).pNucSeq );
        pRet->back( )->iId = std::get<3>( xTup );
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
#endif

}; // namespace libMA

#ifdef WITH_PYTHON
#ifdef WITH_BOOST
void exportSoCDbWriter( );
#else
void exportSoCDbWriter( py::module& rxPyModuleId );
#endif
#endif