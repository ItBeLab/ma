/** 
 * @file fileReader.h
 * @brief Reads queries from a file.
 * @author Markus Schmidt
 */
#ifndef FILE_READER_H
#define FILE_READER_H

#include "module/module.h"
#include "container/nucSeq.h"

/*
 * switches between two modes:
 * ( 1 ): 
 *      There is a seperate thread that uses a buffer in order to read the file.
 *      Worker threads merely collect queries.
 * ( 0 ):
 *      Each worker thread reads the it's own query from the file system (synchronized).
 *      This variant does not use a buffer, but individual stream operations.
 *
 * Testing does not show noticeble differences between the two modes. 
 */
#define USE_BUFFERED_ASYNC_READER ( 0 )

/// @cond DOXYGEN_SHOW_SYSTEM_INCLUDES
#include <fstream>
/// @endcond

namespace libMA
{

    
    class Reader: public Module
    {
    public:
        virtual size_t getCurrPosInFile() const = 0;
        virtual size_t getFileSize() const = 0;
    };// class

    /**
     * @brief Reads Queries from a file.
     * @details
     * Reads (multi-)fasta or fastaq format.
     */
    class FileReader: public Reader
    {
#if USE_BUFFERED_ASYNC_READER == 1
    private:
        /**
         * @brief Helper class.
         * @details
         * This class reads the queries asynchronously and buffers them.
         * This way no thread should need to wait for the filesystem.
         * @note hasNext and next should be accessed by no more than one thread at a time.
         */
        class BufferedReader
        {
            private:
                size_t uiFileBufferSize = 1048576; // == 2^20 ~= 0.1 GB buffer size
                // @note this buffer must be of sufficient size to avoid errors due to overflows
                const size_t uiQueryBufferSize = 250;
                unsigned int uiNucSeqBufPosRead = 0;
                unsigned int uiNucSeqBufPosWrite = 0;
                unsigned int uiCharBufPosRead = uiFileBufferSize;
                std::vector<char> vBuffer;
                std::vector<std::shared_ptr<NucSeq>> vpNucSeqBuffer;
                std::mutex xMutex;
                std::condition_variable cv;
                std::ifstream xFile;
                std::thread xThread;
                inline void reFillBuffer()
                {
                    if(xFile.eof())
                    {
                        throw AlignerException("Unexpected end of file");
                    }// if
                    DEBUG(
                        std::cout << "refilling buffer" << std::endl;
                    )
                    xFile.read(vBuffer.data(), uiFileBufferSize);
                    if(xFile.eof())
                    {
                        uiFileBufferSize = xFile.gcount();
                    }// if
                    uiCharBufPosRead = 0;
                }// function

                inline unsigned int searchEndline()
                {
                    unsigned int uiI = 0;
                    while(
                            uiCharBufPosRead + uiI < uiFileBufferSize &&
                            vBuffer[uiCharBufPosRead + uiI] != '\n'
                          )
                        uiI++;
                    return uiI;
                }// function
            public:

                BufferedReader(std::string sFileName)
                        :
                    vBuffer(uiFileBufferSize),
                    vpNucSeqBuffer(uiQueryBufferSize),
                    xFile(sFileName)
                {
                    if (!xFile.is_open())
                    {
                        throw AlignerException("Unable to open file" + sFileName);
                    }//if
                    // the async file reader
                    DEBUG(
                        std::cout << "Dispatching async reader" << std::endl;
                    )
                    std::mutex xTemp;
                    std::unique_lock<std::mutex> xDispatchLock(xTemp);
                    std::condition_variable xDispatchCv;
                    xThread = std::thread(
                        [&]
                        ()
                        {
                            std::unique_lock<std::mutex> xLock(xMutex);
                            DEBUG(
                                std::cout << "Dispatched async reader" << std::endl;
                            )
                            xDispatchCv.notify_one();
                            while( !xFile.eof() || uiCharBufPosRead < uiFileBufferSize )
                            {
                                while(
                                        ( (uiNucSeqBufPosWrite + 1) % uiQueryBufferSize) 
                                            ==
                                        uiNucSeqBufPosRead
                                    )
                                    cv.wait(xLock);
                                auto pCurr = std::make_shared<NucSeq>();
                                pCurr->sName = "";
                                if(uiCharBufPosRead >= uiFileBufferSize)
                                    reFillBuffer();
                                // find end of name
                                if (! (vBuffer[uiCharBufPosRead] == '>') )
                                {
                                    throw AlignerException("Invalid file format: expecting '>' at query begin");
                                }//if
                                while(true)
                                {
                                    unsigned int uiCharBufPosReadLen = searchEndline();
                                    /// std::cout << "\\n pos " << uiCharBufPosReadLen << std::endl;
                                    for(
                                            unsigned int uiI = uiCharBufPosRead;
                                            uiI < uiCharBufPosRead + uiCharBufPosReadLen;
                                            uiI++
                                        )
                                        pCurr->sName += vBuffer[uiI];
                                    uiCharBufPosRead += uiCharBufPosReadLen;
                                    if(uiCharBufPosRead >= uiFileBufferSize)
                                        reFillBuffer();
                                    else
                                        break;
                                }// while

                                /// std::cout << "<> " << pCurr->sName << std::endl;
                                // remove the description from the query name
                                pCurr->sName = pCurr->sName.substr(1, pCurr->sName.find(' ') - 1);

                                // find end of nuc section
                                while(
                                        (!xFile.eof() && uiCharBufPosRead >= uiFileBufferSize) ||
                                        (uiCharBufPosRead < uiFileBufferSize && vBuffer[uiCharBufPosRead] != '>')
                                    )
                                {
                                    unsigned int uiCharBufPosReadLen = searchEndline();
                                    /// std::cout << "\\n pos " << uiCharBufPosReadLen << std::endl;
                                    //memcpy the data over
                                    pCurr->vAppend(
                                            (const uint8_t*) &vBuffer[uiCharBufPosRead],
                                            uiCharBufPosReadLen
                                        );
                                    uiCharBufPosRead += uiCharBufPosReadLen + 1;
                                    if(!xFile.eof() && uiCharBufPosRead >= uiFileBufferSize)
                                        reFillBuffer();
                                }// while

                                pCurr->vTranslateToNumericForm(0);
                                vpNucSeqBuffer[uiNucSeqBufPosWrite] = pCurr;
                                uiNucSeqBufPosWrite = (uiNucSeqBufPosWrite + 1) % uiQueryBufferSize;
                            }// while
                            xLock.unlock();
                        }// lambda
                    );// std::thread
                    xDispatchCv.wait(xDispatchLock);
                    xDispatchLock.unlock();
                    DEBUG(
                        std::cout << "Main thread free to continue" << std::endl;
                    )
                }// constructor

                ~BufferedReader()
                {
                    //@todo if file is not completely read into the buffer this will not terminate
                    xThread.join();
                    xFile.close();
                }// deconstructor

                /**
                 * @note expects accessing threads to be synchronized
                 */
                bool hasNext()
                {
                    if(uiNucSeqBufPosRead != uiNucSeqBufPosWrite)
                        return true;

                    // no data immediately available -> we have to wait
                    std::lock_guard<std::mutex> xLock(xMutex);
                    return uiNucSeqBufPosRead != uiNucSeqBufPosWrite;
                }// function

                /**
                 * @note expects accessing threads to be synchronized
                 */
                std::shared_ptr<NucSeq> next()
                {
                    auto pQuery = vpNucSeqBuffer[uiNucSeqBufPosRead];
                    vpNucSeqBuffer[uiNucSeqBufPosRead] = nullptr;
                    uiNucSeqBufPosRead = (uiNucSeqBufPosRead + 1) % uiQueryBufferSize;
                    cv.notify_one();
                    assert(pQuery != nullptr);
                    return pQuery;
                }// function
        };// class
    public:
        std::shared_ptr<BufferedReader> pFile;

        /**
         * @brief creates a new FileReader.
         */
        FileReader(std::string sFileName)
                :
            pFile(new BufferedReader(sFileName))
        {
        }//constructor
#else
    public:
        std::shared_ptr<std::ifstream> pFile;
        size_t uiFileSize = 0;
        DEBUG(
            size_t uiNumLinesRead = 0;
            size_t uiNumLinesWithNs = 0;
        )// DEBUG
        //std::shared_ptr<std::mutex> pSynchronizeReading;

        /**
         * @brief creates a new FileReader.
         */
        FileReader(std::string sFileName)
                :
            pFile(new std::ifstream(sFileName))
            //,pSynchronizeReading(new std::mutex)
        {
            if (!pFile->is_open())
            {
                throw AlignerException("Unable to open file" + sFileName);
            }//if
            std::ifstream xFileEnd(sFileName, std::ifstream::ate | std::ifstream::binary);
            uiFileSize = xFileEnd.tellg();
            if(uiFileSize == 0)
                std::cerr << "Warning: using empty file " << sFileName << std::endl;
        }//constructor

        ~FileReader()
        {
            DEBUG(
                std::cout << "read " << uiNumLinesRead << " lines in total." << std::endl;
                std::cout << "read " << uiNumLinesWithNs << " N's." << std::endl;
                if( ! pFile->eof())
                    std::cerr << "WARNING: Did abort before end of File." << std::endl;
            )// DEBUG
            pFile->close();
        }//deconstructor
#endif

        std::shared_ptr<Container> EXPORTED execute(std::shared_ptr<ContainerVector> vpInput);

        /**
         * @brief Used to check the input of execute.
         * @details
         * Returns:
         * - Nil
         */
        ContainerVector EXPORTED getInputType() const;

        /**
         * @brief Used to check the output of execute.
         * @details
         * Returns:
         * - ContainerVector(NucSeq)
         */
        std::shared_ptr<Container> EXPORTED getOutputType() const;

        // @override
        std::string getName() const
        {
            return "FileReader";
        }//function

        // @override
        std::string getFullDesc() const
        {
            return std::string("FileReader");
        }//function

        // @override
        bool outputsVolatile() const
        {
            return true;
        }//function

        // @override
        bool requiresLock() const
        {
            return true;
        }//function

#if USE_BUFFERED_ASYNC_READER == 1
        /**
         * @brief Test the BufferedReader class.
         * @details
         * Writes several sequences to a file; then reads them again and checks for equality.
         * Uses multiple (synchronized) threads for reading.
         */
        static void testBufReader()
        {
            //std::srand(123);
            const unsigned int uiNumTests = 10000;
            for(unsigned int i = 0; i <= uiNumTests; i++)
            {
                const int uiLineLength = std::rand()%25 + 25;
                // generate queries
                std::cout << "generating queries" << std::endl;
                std::vector<std::shared_ptr<NucSeq>> vOriginal;
                for(int j = 0; j < std::rand()%20 + 100; j++)
                {
                    vOriginal.push_back(std::make_shared<NucSeq>());
                    vOriginal.back()->sName = std::to_string(j).append(" some description");
                    for(int j = 0; j < std::rand()%500 + 10; j++)
                    {
                        vOriginal.back()->push_back(std::rand()%5);
                    }// for
                    //make sure N's are encoded correctly
                    vOriginal.back()->vTranslateToCharacterForm(0);
                    vOriginal.back()->vTranslateToNumericForm(0);
                }// for

                // write file
                std::cout << "writing file" << std::endl;
                std::ofstream xOut(".tempTest");
                for(auto pSeq : vOriginal)
                {
                    xOut << pSeq->fastaq_l(uiLineLength);
                }//for
                xOut.flush();
                xOut.close();

                // read file
                std::cout << "reading file" << std::endl;
                const unsigned int uiThreads = 8;
                std::vector<std::vector<std::shared_ptr<NucSeq>>> vReads(uiThreads);
                BufferedReader xIn(".tempTest");
                std::mutex xMutex;

                {
                    ThreadPool xTp(uiThreads);
                    for(unsigned int uiT = 0; uiT < uiThreads; uiT++)
                        xTp.enqueue(
                            [&]
                            (size_t uiTid, unsigned int uiX)
                            {
                                do
                                {
                                    std::lock_guard<std::mutex> xGuard(xMutex);
                                    if(!xIn.hasNext())
                                        break;
                                    vReads[uiX].push_back(xIn.next());
                                } while(true);
                            },//lambda
                            uiT
                        );//enqueue
                }//scope for threadpool

                //merge sequences
                std::vector<std::shared_ptr<NucSeq>> vRead;
                for(auto vMerge : vReads )
                    for(auto pEle : vMerge)
                        vRead.push_back(pEle);
                std::sort(
                    vRead.begin(), vRead.end(),
                    []
                    (std::shared_ptr<NucSeq> pA, std::shared_ptr<NucSeq> pB)
                    {
                        try{
                            return std::stoi(pA->sName) < std::stoi(pB->sName);
                        }
                        catch(...)
                        {
                            std::cout << "error on stoi: "
                                << pA->sName << " ?< " << pB->sName << std::endl;
                            return false;
                        }
                    }//lambda
                );//sort


                // check
                std::cout << "checking" << std::endl;
                if(vOriginal.size() != vRead.size())
                {
                    std::cout << "[error] got different sizes " << vOriginal.size() 
                            << " != " << vRead.size() << std::endl;
                    return;
                }//if
                for(unsigned int i=0; i < vOriginal.size(); i++)
                {
                    if(!vOriginal[i]->equal(*vRead[i]))
                    {
                        std::cout << "[error] got different sequences \n"
                                << vOriginal[i]->fastaq_l(50)
                                << " != \n"
                                << vRead[i]->fastaq_l(50)
                                << std::endl;
                        return;
                    }// if
                }//for
                std::cout << "[OK] " << i << "/" << uiNumTests << std::endl;
            }// for
        }// function
        size_t getCurrPosInFile() const
        {
            return 1;
        }// function

        size_t getFileSize() const
        {
            return 1;
        }// function
#else
        size_t getCurrPosInFile() const
        {
            if(!pFile->good() || pFile->eof())
                return uiFileSize;
            return pFile->tellg();
        }// function

        size_t getFileSize() const
        {
            // prevent floating point exception here (this is only used for progress bar...)
            if(uiFileSize == 0)
                return 1;
            return uiFileSize;
        }// function
#endif
    private:
        /**
         * code taken from 
         * https://stackoverflow.com/questions/6089231/getting-std-ifstream-to-handle-lf-cr-and-crlf
         * suports windows linux and mac line endings
         */
        inline void safeGetline(std::string& t)
        {
            t.clear();
            DEBUG(
                uiNumLinesRead++;
            )// DEBUG

            // The characters in the stream are read one-by-one using a std::streambuf.
            // That is faster than reading them one-by-one using the std::istream.
            // Code that uses streambuf this way must be guarded by a sentry object.
            // The sentry object performs various tasks,
            // such as thread synchronization and updating the stream state.

            std::istream::sentry se(*pFile, true);
            std::streambuf* sb = pFile->rdbuf();

            for(;;) {
                int c = sb->sbumpc();
                switch (c) {
                case '\n':
                    return;
                case '\r':
                    if(sb->sgetc() == '\n')
                        sb->sbumpc();
                    return;
                case std::streambuf::traits_type::eof():
                    // Also handle the case when the last line has no line ending
                    if(t.empty())
                        pFile->setstate(std::ios::eofbit);
                    return;
                default:
                    t += (char)c;
                }
            }
        }// method
    };//class
    /**
     * @brief Reads Queries from a file.
     * @details
     * Reads (multi-)fasta or fastaq format.
     */
    class PairedFileReader: public Reader
    {
    public:
        FileReader xF1;
        FileReader xF2;

        /**
         * @brief creates a new FileReader.
         */
        PairedFileReader(std::string sFileName1, std::string sFileName2)
                :
            xF1(sFileName1),
            xF2(sFileName2)
        {
            if(xF1.getFileSize() != xF2.getFileSize())
                std::cerr << "Paired alignment with differently sized files." << std::endl;
        }//constructor

        std::shared_ptr<Container> EXPORTED execute(std::shared_ptr<ContainerVector> vpInput);

        /**
         * @brief Used to check the input of execute.
         * @details
         * Returns:
         * - Nil
         */
        ContainerVector EXPORTED getInputType() const;

        /**
         * @brief Used to check the output of execute.
         * @details
         * Returns:
         * - ContainerVector(NucSeq)
         */
        std::shared_ptr<Container> EXPORTED getOutputType() const;

        // @override
        std::string getName() const
        {
            return "PairedFileReader";
        }//function

        // @override
        std::string getFullDesc() const
        {
            return std::string("PairedFileReader");
        }//function

        // @override
        bool outputsVolatile() const
        {
            return true;
        }//function

        // @override
        bool requiresLock() const
        {
            return true;
        }//function

        size_t getCurrPosInFile() const
        {
            return xF1.getCurrPosInFile() + xF2.getCurrPosInFile();
        }// function

        size_t getFileSize() const
        {
            return xF1.getFileSize() + xF2.getFileSize();
        }// function
    };//class

}//namespace

#ifdef WITH_PYTHON
void exportFileReader();
#endif

#endif