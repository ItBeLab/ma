#include "module/mappingQuality.h"

using namespace libMABS;

extern int iMatch;

ContainerVector MappingQuality::getInputType() const
{
    return ContainerVector{
        std::shared_ptr<Container>(new NucSeq()),
        std::shared_ptr<Container>(new ContainerVector(std::shared_ptr<Alignment>(new Alignment())))
    };
}//function

std::shared_ptr<Container> MappingQuality::getOutputType() const
{
    return std::shared_ptr<Container>(new ContainerVector(std::shared_ptr<Alignment>(new Alignment())));
}//function

std::shared_ptr<Container> MappingQuality::execute(
        std::shared_ptr<ContainerVector> vpInput
    )
{
    std::shared_ptr<NucSeq> pQuery = std::static_pointer_cast<NucSeq>((*vpInput)[0]);
    std::shared_ptr<ContainerVector> pAlignments = std::static_pointer_cast<ContainerVector>((*vpInput)[1]);

    std::shared_ptr<Alignment> pFirst = std::static_pointer_cast<Alignment>((*pAlignments)[pAlignments->size()-1]);

    //mapping quality bast on scores
    if(pAlignments->size() >= 2)
    {
        std::shared_ptr<Alignment> pSecond = std::static_pointer_cast<Alignment>((*pAlignments)[pAlignments->size()-2]);

        pFirst->fMappingQuality =
                ( pFirst->score() - std::max(0, pSecond->score()) )
                    /
                (double)(iMatch * pQuery->length())
            ;

    }//if
    else
        pFirst->fMappingQuality = pFirst->score() / (double)(iMatch * pQuery->length());

    //factors
    //penalty for too little seeds
    double dA = std::max(std::min(10 * pFirst->numBySeeds() / (double)pQuery->length(), 1.0), 0.1);
    pFirst->fMappingQuality *= dA;


    return std::shared_ptr<ContainerVector>(new ContainerVector(pAlignments));
}//function

void exportMappingQuality()
{
    //export the MappingQuality class
    boost::python::class_<
            MappingQuality, 
            boost::python::bases<Module>, 
            std::shared_ptr<MappingQuality>
        >("MappingQuality")
    ;

    boost::python::implicitly_convertible< 
        std::shared_ptr<MappingQuality>,
        std::shared_ptr<Module> 
    >();
}//function
