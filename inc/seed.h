#include "container.h"

//any index on the query or reference nucleotide sequence is given in this datatype
typedef uint64_t nucSeqIndex;


class Seed: public Container, public Interval<nucSeqIndex>{
private:
	////the beginning of the match on the reference
	const nucSeqIndex uiPosOnReference;

public:
	Seed(
            const nucSeqIndex uiPosOnQuery, 
            const nucSeqIndex uiLenght, 
            const nucSeqIndex uiPosOnReference
        )
            :
        Interval(uiPosOnQuery, uiLenght),
        uiPosOnReference(uiPosOnReference)
    {}//constructor
    
    nucSeqIndex start_ref() const
    {
        return uiPosOnReference;
    }//function
    
    nucSeqIndex end_ref() const
    {
        return uiPosOnReference + size();
    }//function
}; //class


class SeedContainer: public Container{
private:
	//the actual seed from the segmentation step
	std::shared_ptr<const Seed> pxSeed;
	//is the seed enabled in this bucket?
    bool bEnabled;
public:
    SeedContainer(std::shared_ptr<const Seed> pxSeed)
            :
        pxSeed(pxSeed),
        bEnabled(true)
    {}//constructor
    
    inline const Seed& operator*() const
    {
        return *pxSeed;
    }//operator
    inline const Seed& operator->() const
    {
        return *pxSeed;
    }//operator
};//class