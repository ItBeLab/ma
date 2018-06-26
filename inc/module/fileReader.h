/** 
 * @file fileReader.h
 * @brief Reads queries from a file.
 * @author Markus Schmidt
 */
#ifndef FILE_READER_H
#define FILE_READER_H

#include "module/module.h"
#include "container/nucSeq.h"

/// @cond DOXYGEN_SHOW_SYSTEM_INCLUDES
#include <fstream>
/// @endcond

namespace libMA
{

    /**
     * @brief Reads Queries from a file.
     * @details
     * Reads (multi-)fasta or fastaq format.
     */
    class FileReader: public Module
    {
    private:
        class BufferedReader
        {
            private:
                size_t uiFileBufferSize = 1048576; // == 2^20 ~= 0.1 GB buffer size
                const size_t uiQueryBufferSize = 100;
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
                                do
                                {
                                    unsigned int uiCharBufPosReadLen = searchEndline();
                                    for(
                                            unsigned int uiI = uiCharBufPosRead;
                                            uiI < uiCharBufPosRead + uiCharBufPosReadLen;
                                            uiI++
                                        )
                                        pCurr->sName += vBuffer[uiI];
                                    uiCharBufPosRead += uiCharBufPosReadLen + 1;
                                    if(uiCharBufPosRead >= uiFileBufferSize)
                                        reFillBuffer();
                                }// do
                                while(uiCharBufPosRead >= uiFileBufferSize);

                                // remove the description from the query name
                                pCurr->sName = pCurr->sName.substr(1, pCurr->sName.find(' '));

                                // find end of nuc section
                                do
                                {
                                    unsigned int uiCharBufPosReadLen = searchEndline();
                                    //memcpy the data over
                                    pCurr->vAppend(
                                            (const uint8_t*) &vBuffer[uiCharBufPosRead],
                                            uiCharBufPosReadLen
                                        );
                                    uiCharBufPosRead += uiCharBufPosReadLen + 1;
                                    if(!xFile.eof() && uiCharBufPosRead >= uiFileBufferSize)
                                        reFillBuffer();
                                }// do
                                while(
                                        (!xFile.eof() && uiCharBufPosRead >= uiFileBufferSize) ||
                                        (uiCharBufPosRead < uiFileBufferSize && vBuffer[uiCharBufPosRead] != '>')
                                    );

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

        bool outputsVolatile() const
        {
            return true;
        }//function

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
                        return std::stoi(pA->sName) < std::stoi(pB->sName);
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

    };//class

}//namespace

#ifdef WITH_PYTHON
void exportFileReader();
#endif

#endif