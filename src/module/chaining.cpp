#include "module/chaining.h"
using namespace libMA;

ContainerVector Chaining::getInputType() const
{
    return ContainerVector{
            //the strip of consideration
            std::shared_ptr<Container>(new Seeds()),
        };
}//function

std::shared_ptr<Container> Chaining::getOutputType() const
{
    return std::shared_ptr<Container>(new ContainerVector(std::shared_ptr<Container>(new Seeds())));
}//function


std::shared_ptr<Container> Chaining::execute(
        std::shared_ptr<ContainerVector> vpInput
    )
{
    std::shared_ptr<Seeds> pSeeds = std::static_pointer_cast<Seeds>((*vpInput)[0]);

    //take care of the two special cases
    if(pSeeds->size() == 0)
        return std::shared_ptr<Container>(new ContainerVector{std::shared_ptr<Seeds>(new Seeds())});
    if(pSeeds->size() == 1)
    {
        std::shared_ptr<Seeds> ret = std::shared_ptr<Seeds>(new Seeds());
        ret->push_back( pSeeds->front() );
        return std::shared_ptr<Container>(new ContainerVector{ret});
    }//if

    //normal case where we have >= 2 seeds and actually need to make a decision on what to keep.

    //start with setting up the Range Maximum Query (RMQ) data structures
    //one for the first octant one for the second octant
    std::vector<RMQ<int64_t>::RMQData> data1;
    std::vector<RMQ<int64_t>::RMQData> data2;
    int64_t veryVerySmall = 9*(INT64_MIN/10);
    //we will put two dummy starting points in the RMQs.
    //that way we can be sure that any query will always have a result. 
    data1.push_back(RMQ<int64_t>::RMQData(veryVerySmall,0,nullptr, veryVerySmall));
    data2.push_back(RMQ<int64_t>::RMQData(0,veryVerySmall,nullptr, veryVerySmall));

    //now we add all seeds to both RMQs
    std::vector<std::shared_ptr<Chain>> chains;
    for(Seed seed : *pSeeds)
    {
        std::shared_ptr<Chain> chain = std::shared_ptr<Chain>(new Chain(seed));
        chains.push_back(chain);
        data1.push_back(t1(seed, chain));
        data2.push_back(t2(seed, chain));
    }//for

    //first octant
    RMQ<int64_t> d1 = RMQ<int64_t>(data1);
    //2nd octant
    RMQ<int64_t> d2 = RMQ<int64_t>(data2);

    //it's importent to do this after the initialization of the trees
    //sonce the trees sort the vector datastructure...
    for(unsigned int i = 0; i < data1.size(); i++)
        if(data1[i].pChain != nullptr)
            data1[i].pChain->t1 = &data1[i];
    for(unsigned int i = 0; i < data2.size(); i++)
        if(data2[i].pChain != nullptr)
            data2[i].pChain->t2 = &data2[i];
    std::shared_ptr<Chain> bestChain = chains[0];



    std::sort(
        chains.begin(), chains.end(),
        []
        (const std::shared_ptr<Chain> a, const std::shared_ptr<Chain> b)
        {
            if(a->s.end_ref() == b->s.end_ref())
                return a->s.end() < b->s.end();
            return a->s.end_ref() < b->s.end_ref();
        }//lambda
    );//function call

    //the actual chaining
    for(std::shared_ptr<Chain> chain : chains)
    {
        DEBUG_2(
            std::cout << "computing chain for (ref/query/size): " << chain->s.end_ref() << " "
                    << chain->s.end() << " "
                    << chain->s.size() << std::endl;
        )
    /*
    * SWITCH between allowing overlaps of chains our not
    * true = no overlaps
    * WARNING: chaining code is not 100% correct when allowing overlaps
    * (basically each certain match will be scored as possible match only in that case)
    */ 
    #define STARTS 1
    #if STARTS
        RMQ<int64_t>::RMQData& a = d1.rmq(
                veryVerySmall,-1,
                (int64_t)chain->s.start_ref()-(int64_t)chain->s.start() , chain->s.start() //@todo  replace with function
            );
        RMQ<int64_t>::RMQData& b = d2.rmq(
                -1,(int64_t)chain->s.start()-(int64_t)chain->s.start_ref(),
                chain->s.start_ref() ,veryVerySmall //@todo  replace with function
            );
    #else
        RMQ<int64_t>::RMQData& a = d1.rmq(
                veryVerySmall,-1,
                (int64_t)chain->s.end_ref()-(int64_t)chain->s.end() - 1, chain->s.end() - 1//@todo  replace with function
            );
        RMQ<int64_t>::RMQData& b = d2.rmq(
                -1,(int64_t)chain->s.end()-(int64_t)chain->s.end_ref() - 1,
                chain->s.end_ref() - 1,veryVerySmall //@todo  replace with function
            );
    #endif
        DEBUG_2(
            std::cout << "score adjustments (second/first octant): -"
                    << gc1_start(chain->s) << " -"
                    << gc2_start(chain->s) << std::endl;
        )
        //using the RMQdata's to check for smaller insted of the chains
        //since we do not have to check for nullptrs this way
    #if STARTS
        if(a.score - gc1_start(chain->s) < b.score - gc2_start(chain->s))
    #else
        if(a.score - gc1_end(chain->s) < b.score - gc2_end(chain->s))
    #endif
            chain->pred = b.pChain;
        else
            chain->pred = a.pChain;

        if(chain->pred == nullptr)
        {
            DEBUG_2(
                std::cout << "picked dummy" << std::endl;
            )
            continue;
        }//if
        assert(chain->pred != chain);
        DEBUG_2(
            std::cout << "candidate: " << chain->pred->s.end_ref() << " "
                    << chain->pred->s.end() << " "
                    << chain->pred->s.size() << std::endl;
        )
        assert(chain->pred->s.end_ref() <= chain->s.end_ref());
        assert(chain->pred->s.end() <= chain->s.end());
        //update score
        int64_t x = chain->s.end_ref() - chain->pred->s.end_ref();
        int64_t y = chain->s.end() - chain->pred->s.end();

        int64_t possibleMatches = std::min(x,y);
        int64_t certainMatches = std::min(possibleMatches, (int64_t)chain->s.size());
        possibleMatches -= certainMatches;
        int64_t insOrDls = std::max(x,y) - std::min(x,y);

        //set the score for the new chain
        int64_t addScore = (certainMatches * SCORE_MATCH
                - possibleMatches * COST_POSS_MATCH)
                - insOrDls * COST_INS_DEL;

        DEBUG_2(
            std::cout << "best candidate would require " << insOrDls << " insertions or deletions "
            << possibleMatches << " possible matches and " << certainMatches << " certain matches for a score of " << chain->pred->score << "; add score is: " << addScore << std::endl;
        )

        //if chaining this seed results in a worse chain then don't...
        //this makes our global chaining into a local chaining
        if(chain->score > addScore + chain->pred->score)
        {
            DEBUG_2(
                std::cout << "best chain not worth"<< std::endl;
            )
            chain->pred = nullptr;
        }//if
        else{
            chain->score = addScore + chain->pred->score;
            chain->t1->score = chain->score + gc1_end(chain->s);
            chain->t2->score = chain->score + gc2_end(chain->s);
        }//else


        DEBUG_2(
            std::cout << "current score: "<< chain->score << std::endl;
        )


        //remember best chain
        if(*bestChain < *chain)
            bestChain = chain;
    }//for

    std::shared_ptr<Seeds> pRet = std::shared_ptr<Seeds>(new Seeds());

//toggle for generating debug output
#if 0
    for(std::shared_ptr<Chain> chain : chains)
    {
        if(chain->pred != nullptr)
        {
            pRet->push_front(chain->s);
            pRet->push_front(chain->pred->s);
            pRet->push_front(Seed(0,0,0));
        }
    }//for
#endif

    while(bestChain != nullptr)
    {
        DEBUG_2(
            std::cout << "solution:" << std::endl;
            std::cout << bestChain->s.start() << " "
                    << bestChain->s.start_ref() << " "
                    << bestChain->s.size() << std::endl;
        )
        //@todo  possible that this gives reverse output...
        pRet->push_back(bestChain->s);
        bestChain = bestChain->pred;
    }//while
    
    //seeds need to be sorted for the following steps
    std::sort(
            pRet->begin(), pRet->end(),
            [](const Seed& xA, const Seed& xB)
            {
                if(xA.start_ref() == xB.start_ref())
                    return xA.start() < xB.start();
                return xA.start_ref() < xB.start_ref();
            }//lambda
        );//sort function call
    
    DEBUG_2(
        std::cout << "done" << std::endl;
    )
    return std::shared_ptr<Container>(new ContainerVector{pRet});
}//function

void exportChaining()
{
    //export the LineSweepContainer class
    boost::python::class_<
        Chaining, 
        boost::python::bases<Module>,
        std::shared_ptr<Chaining>
        >("Chaining");
    boost::python::implicitly_convertible< 
        std::shared_ptr<Chaining>,
        std::shared_ptr<Module> 
    >();
}//function
