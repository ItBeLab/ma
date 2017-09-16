/** 
 * @file intervalTree.h
 * @brief Implements a the IntervalTree used for segmentation and various other related classes.
 * @author Markus Schmidt
 */
#ifndef INTERVALTREE_H
#define INTERVALTREE_H

#include "fm_index.h"
#include <thread>
#include "doublyLinkedList.h"
#include "seed.h"

#define confMETA_MEASURE_DURATION ( 1 )


/**
 * @brief A Suffix Array Segment.
 * @details
 * A Suffix Array Segment is made up of two Intervals.
 * @li @c a SA_IndexInterval.
 * @li @c a Interval representing the position of the sequence on the query.
 */
class SaSegment: public Container, public Interval<nucSeqIndex> {
private:
	SA_IndexInterval xSaInterval;
	bool bForw;
public:
	/**
	* @brief Creates a new SaSegment.
	* @details Creates a new SaSegment on the base of a SA_IndexInterval and the 
	* respective indices on the quey.
	*/
	SaSegment(nucSeqIndex uiStart, nucSeqIndex uiSize, SA_IndexInterval xSaInterval, bool bForw)
			:
		Interval(uiStart, uiSize),
		xSaInterval(xSaInterval),
		bForw(bForw)
	{}//constructor

	//overload
	ContainerType getType() const {return ContainerType::segment;}
	/**
	 * @brief The bwt interval within.
	 * @returns the bwt interval within.
	 */
	const SA_IndexInterval& saInterval() const
	{
		return xSaInterval;
	}//function
	/**
	 * @brief Weather the segment was created by a forward extension or a backawards extension.
	 * @returns true if the segment was created by forward extension.
	 * @details
	 * The forwards extension is implemented by backwards extension on an reversed FM_Index.
	 */
	bool isForward() const
	{
		return bForw;
	}//function
}; // class ( Segment )

/**
 * @brief A Interval in the Segment Tree.
 */
class SegmentTreeInterval: public Container, public Interval<nucSeqIndex>
{
private:
	/** 
	 * @brief list of the perfect matches found through backwards / forward extension 
	 */
	std::list<SaSegment> lxSaSegment;
	/** 
	 * @brief list of the longest perfect matches found through backwards / forward extension 
	 */
	std::list<SaSegment> lxSaAnchorSegment;

public:
	/**
	 * @brief Creates a new interval with a start and size.
	 */
	SegmentTreeInterval(const nucSeqIndex uiStart, const nucSeqIndex uiSize)
		:
		Interval(uiStart, uiSize),
		lxSaSegment(),
		lxSaAnchorSegment()
	{}//constructor
	
	//overload
	ContainerType getType(){return ContainerType::segment;}//function


	/**
	 * @brief Prints information about this node.
	 * @note Thread save.
	 */
	void print(std::ostream& xOs) const
	{
		xOs << "(" << std::to_string(this->start()) << "," << std::to_string(this->end()) << ")";
	}//function
	/**
	 * @brief Push back an interval of perfect matches.
	 * @details
	 * The interval contains uiLengthInBwt individual perfect matches of 
	 * (uiStartOfIntervalOnQuery, uiEndOfIntervalOnQuery) on the reference sequence.
	 */
	void push_back(SaSegment interval, bool bAnchor);

	/**
	 * @brief The center of the segment.
	 * @returns the center of the segment.
	 */
	nucSeqIndex getCenter() const 
	{
		return start() + size() / 2; 
	}//function

	/**
	 * @brief Extracts all seeds from the tree.
	 * @details
	 * Calls fDo for all recorded hits.
	 * @Note pushBackBwtInterval records an interval of hits
	 */
	void forEachSeed(
			std::shared_ptr<FM_Index> pxFM_Index,
			std::shared_ptr<FM_Index> pxRev_FM_Index,
			unsigned int uiMaxNumHitsPerInterval,
			bool bSkipLongerIntervals,
			bool bAnchorOnly,
			std::function<void(Seed s)> fDo
		)
	{
		//iterate over all the intervals that have been recorded using pushBackBwtInterval()
		for (SaSegment xSegment : bAnchorOnly ? lxSaAnchorSegment : lxSaSegment)
		{
			//if the interval contains more than uiMaxNumHitsPerInterval hits it's of no importance and will produce nothing but noise

			//if bSkipLongerIntervals is not set uiJump by is used to not return more than 
			//uiMaxNumHitsPerInterval
			t_bwtIndex uiJumpBy = 1;
			if (xSegment.saInterval().size() > uiMaxNumHitsPerInterval)
			{
				if (bSkipLongerIntervals)
					continue;
				uiJumpBy = xSegment.saInterval().size() / uiMaxNumHitsPerInterval; 
			}//if

			//if the hit was generated using the reversed fm_index we should use the according fm_index in order to 
			//extract the index of the hit on the reference sequence. same for the forward fm_index
			std::shared_ptr<FM_Index> pxUsedFmIndex;
			if (xSegment.isForward())
				pxUsedFmIndex = pxRev_FM_Index;
			else
				pxUsedFmIndex = pxFM_Index;

			//iterate over the interval in the BWT
			for (
					auto ulCurrPos = xSegment.saInterval().start(); 
					ulCurrPos < xSegment.saInterval().end(); 
					ulCurrPos += uiJumpBy
				)
			{
				//calculate the referenceIndex using pxUsedFmIndex->bwt_sa() and call fDo for every match individually
				auto ulIndexOnRefSeq = pxUsedFmIndex->bwt_sa(ulCurrPos);
				/* if the match was calculated using the fm-index of the reversed sequence:
				 * we acquire the index of the beginning of the match on the reversed sequence by calling bwt_sa()
				 * but we actually want the beginning of the match on the normal sequence, so we need to subtract the END of the match from the reference sequence length
				 */
				if (xSegment.isForward())
					ulIndexOnRefSeq = pxUsedFmIndex->getRefSeqLength() - (ulIndexOnRefSeq + xSegment.size()) - 1;
				assert(xSegment.start() < xSegment.end());
				//call the given function
				fDo(Seed(xSegment.start(), xSegment.size() + 1, ulIndexOnRefSeq));
			}//for
		}//for
	}//function

	/**
	 * @brief Returns all seeds from the tree.
	 * @details
	 * As opposed to forEachSeed the seeds get collected and returned in a vector.
	 */
	std::vector<std::shared_ptr<NucleotideSequence>> getRefHits(
			std::shared_ptr<FM_Index> pxFM_Index, 
			std::shared_ptr<FM_Index> pxRev_FM_Index,
			std::shared_ptr<BWACompatiblePackedNucleotideSequencesCollection> pxRefPack
		)
	{
		std::vector<std::shared_ptr<NucleotideSequence>> vpRet = 
			std::vector<std::shared_ptr<NucleotideSequence>>();
		forEachSeed(
			pxFM_Index,
			pxRev_FM_Index,
			100000,
			false,
			false,
			[&](Seed xS)
			{
				vpRet.push_back(pxRefPack->vExtract(xS.start_ref(), xS.end_ref()));
			}//lambda
		);//forall
		return vpRet;
	}//function
};//class

/**
 * @brief The segment tree.
 * @details
 * The segment "tree" is actually a doubly linked list.
 * The tree only exists logically,
 * meaning that the segments within the list represent the first layer of the tree initally.
 * Then after each iteration, the segments within the list represent the next layer down of the 
 * tree.
 */
class SegmentTree : public DoublyLinkedList<SegmentTreeInterval>, public Container{

public:
	/**
	* @brief Creates a new tree containing one initial segment as root.
	* @details
	* Sets up the interval tree with two leaves and one initial interval comprising the whole query
	* note that the tree is internally represented as a DoublyLinkedList since only the leaves are of relevance
	*/
	SegmentTree(const nucSeqIndex uiQuerryLength)
		:
		DoublyLinkedList()
	{
		std::shared_ptr<SegmentTreeInterval> pxRoot(new SegmentTreeInterval(0, uiQuerryLength));
		push_back(pxRoot);
	}//constructor
	SegmentTree()
	{}//constructor
	
	//overload
	ContainerType getType(){return ContainerType::segmentList;}//function

	/**
	 * @brief Prints basic information about the segment tree.
	 * @details
	 * Not thread save.
	 */
	void print(std::ostream &xOut) const
	{
		forEach([&xOut](std::shared_ptr<SegmentTreeInterval> pxNode){ pxNode->print(xOut); });
	}//function
};

/**
 * @brief Simple printer function for the SegmentTree.
 */
std::ostream& operator<<(std::ostream& xOs, const SegmentTree& rxTree);
/**
 * @brief Simple printer function for a SegmentTreeInterval.
 */
std::ostream& operator<<(std::ostream& xOs, const SegmentTreeInterval &rxNode);

/**
 * @brief Exposes the SegmentTree to boost python.
 */
void exportIntervalTree();


#endif