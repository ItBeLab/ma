/** 
 * @file extractAllSeeds.cpp
 * @author Markus Schmidt
 */
#include "module/extractAllSeeds.h"
using namespace libMA;


ContainerVector ExtractAllSeeds::getInputType() const
{
    return ContainerVector
    {
        //all segments
        std::shared_ptr<Container>(new SegmentVector()),
        //the forward fm_index
        std::shared_ptr<Container>(new FMIndex())
    };
}//function

std::shared_ptr<Container> ExtractAllSeeds::getOutputType() const
{
    return std::shared_ptr<Container>(new Seeds());
}//function



std::shared_ptr<Container> ExtractAllSeeds::execute(
        std::shared_ptr<ContainerVector> vpInput
    )
{
    std::shared_ptr<SegmentVector> pSegments = std::static_pointer_cast<SegmentVector>((*vpInput)[0]);
    std::shared_ptr<FMIndex> pFM_index = std::static_pointer_cast<FMIndex>((*vpInput)[1]);

    //extract function is actually built into SegmentVector
    return pSegments->extractSeeds(pFM_index, maxAmbiguity);
}//function

void exportExtractAllSeeds()
{
    //export the ExtractAllSeeds class
    boost::python::class_<
            ExtractAllSeeds,
            boost::python::bases<Module>,
            std::shared_ptr<ExtractAllSeeds>
        >("ExtractAllSeeds")
        .def(boost::python::init<unsigned int>())
        .def_readwrite("max_ambiguity", &ExtractAllSeeds::maxAmbiguity)
    ;

    boost::python::implicitly_convertible< 
        std::shared_ptr<ExtractAllSeeds>,
        std::shared_ptr<Module> 
    >();
}//function