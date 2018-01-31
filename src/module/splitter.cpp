#include "module/splitter.h"

using namespace libMA;

ContainerVector Splitter::getInputType() const
{
    return ContainerVector{std::shared_ptr<Container>(new Nil())};
}//function

std::shared_ptr<Container> Splitter::getOutputType() const
{
    std::shared_ptr<ContainerVector> pContent =
        std::static_pointer_cast<ContainerVector>(pVec->getType());
    return pContent->contentType;
}//function

std::shared_ptr<Container> Splitter::execute(std::shared_ptr<ContainerVector> vpInput)
{
    std::shared_ptr<ContainerVector> pContent =
        std::static_pointer_cast<ContainerVector>(pVec->get());
    if(pContent->empty())
    {
        std::shared_ptr<Nil> pRet(new Nil());
        pRet->bDry = true;
        return pRet;
    }//if
    auto pRet = pContent->back();
    pContent->pop_back();
    return pRet;
}//function

ContainerVector Collector::getInputType() const
{
    return ContainerVector{pVec->contentType};
}//function

std::shared_ptr<Container> Collector::getOutputType() const
{
    return std::shared_ptr<Container>(new Nil());
}//function

std::shared_ptr<Container> Collector::execute(std::shared_ptr<ContainerVector> vpInput)
{
    //synchronize container collection
    if ((*vpInput)[0]->bDry)
        return std::shared_ptr<Container>(new Nil());
    std::lock_guard<std::mutex> xGuard(*pLock);
    pVec->push_back((*vpInput)[0]);
    return std::shared_ptr<Container>(new Nil());
}//function

ContainerVector Lock::getInputType() const
{
    return ContainerVector{pType};
}//function

std::shared_ptr<Container> Lock::getOutputType() const
{
    return pType;
}//function

std::shared_ptr<Container> Lock::execute(std::shared_ptr<ContainerVector> vpInput)
{
    DEBUG(
        std::cout << "lock" << std::endl;
    )
    //locking in the container is done automatically by the pledge
    return (*vpInput)[0];
}//function

ContainerVector UnLock::getInputType() const
{
    //any input type
    return ContainerVector{std::shared_ptr<Container>(new Container())};
}//function

std::shared_ptr<Container> UnLock::getOutputType() const
{
    return std::shared_ptr<Container>(new Nil());
}//function

std::shared_ptr<Container> UnLock::execute(std::shared_ptr<ContainerVector> vpInput)
{
    DEBUG(
        std::cout << "unlock" << std::endl;
    )
    //unlock the given lock
    pLockPledge->set(nullptr);
    pLockPledge->forAllSyncs(
        []
        (std::shared_ptr<Pledge> pSync)
        {
            pSync->set(nullptr);
        }//lambda
    );//for all function call
    return std::shared_ptr<Container>(new Nil());
}//function

void exportSplitter()
{
    //export the Splitter class
    boost::python::class_<
            Splitter, 
            boost::python::bases<Module>, 
            std::shared_ptr<Splitter>
        >("Splitter", boost::python::init<std::shared_ptr<Pledge>>())
    ;

    boost::python::implicitly_convertible< 
        std::shared_ptr<Splitter>,
        std::shared_ptr<Module> 
    >();
    //export the Splitter class
    boost::python::class_<
            Collector, 
            boost::python::bases<Module>, 
            std::shared_ptr<Collector>
        >("Collector", boost::python::init<std::shared_ptr<Container>>())
        .def_readwrite("content", &Collector::pVec)
    ;

    boost::python::implicitly_convertible< 
        std::shared_ptr<Collector>,
        std::shared_ptr<Module> 
    >();

    //export the Lock class
    boost::python::class_<
            Lock, 
            boost::python::bases<Module>, 
            std::shared_ptr<Lock>
        >("Lock", boost::python::init<std::shared_ptr<Container>>())
    ;

    boost::python::implicitly_convertible< 
        std::shared_ptr<Lock>,
        std::shared_ptr<Module> 
    >();

    //export the Lock class
    boost::python::class_<
            UnLock, 
            boost::python::bases<Module>, 
            std::shared_ptr<UnLock>
        >("UnLock", boost::python::init<std::shared_ptr<Pledge>>())
    ;

    boost::python::implicitly_convertible< 
        std::shared_ptr<UnLock>,
        std::shared_ptr<Module> 
    >();

}//function