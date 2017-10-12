/** 
 * @file sequence.h
 * @brief Implements NucleotideSequence.
 * @author Arne Kutzner
 */
#pragma once

#include <memory>
#include <algorithm>
#include <array>
#include <numeric>
#include <cmath>
#include <cstring>
#include "interval.h"
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/log/trivial.hpp>
#include "container.h"

class GeneticSequence;
class NucleotideSequence;

/** 32bit rounding to the next exponent as define
 */
#ifndef kroundup32
#define kroundup32(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))
#endif

/** Generic reverse function, as it occurs in std::algorithms
 */
template <class T>
void reverse(T word[], size_t length)
{
	char temp;
	for ( size_t i = 0; i < length / 2; i++ )
	{
		temp = word[i];
		word[i] = word[length - i - 1 ];
		word[length - i - 1] = temp;
	} // for
} // reverse

/** 
 * @brief Class for the management of sequences (genetic or text).
 * @details
 * Special string class, for sequence handling. 
 */
template <class ELEMENT_TYPE>
class PlainSequence
{
private :
	friend GeneticSequence;

	/** Normally we avoid the copy of PlainSequence Objects, except in the context of the GeneticSequence class.
	 */
	PlainSequence( const PlainSequence &rSequence )
	{
		/** We reset all attributes of our fresh sequence object.
		 */
		vResetProtectedAttributes();

		/** Now we copy all elements from the source sequence to the current sequence.
		 */
		vAppend( rSequence.pGetSequenceRef(), rSequence.uxGetSequenceSize() );
	} // copy constructor

protected :
	/** The encapsulated sequence
	 */
	ELEMENT_TYPE *pxSequenceRef;

	/** Current size of the content of the encapsulated sequence
	 */
	size_t uiSize;

	/** Current size of the buffer.
	 */
	size_t uxCapacity;

	/** Resets all protected attributes to its initial values.
	 */
	inline void vReleaseMemory()
	{
		/** Allocated memory will be released!
		 */
		if ( pxSequenceRef != NULL ) 
		{
			free( pxSequenceRef );
		} // if
	} // protected method

	void vResetProtectedAttributes()
	{
		pxSequenceRef = NULL;
		uiSize = 0;
		uxCapacity = 0;
	} // protected method
	
	/** Tries to allocate the requested amount of memory and throws an exception if this process fails.
	 * uxRequestedSize is expressed in "number of requested elements.
	 */
	void vReserveMemory( size_t uxRequestedSize )
	{
		/* TO DO: This should be a bit more sophisticated ...
		 */
		kroundup32( uxRequestedSize );
		
		/* We try to reserve the requested memory.
		 * See: http://stackoverflow.com/questions/1986538/how-to-handle-realloc-when-it-fails-due-to-memory
		 */
		auto pxReallocRef = (ELEMENT_TYPE*)realloc( pxSequenceRef, uxRequestedSize * sizeof(ELEMENT_TYPE) );

		if ( pxReallocRef == NULL )
		{
			throw fasta_reader_exception( (std::string( "Memory Reallocation Failed for requested size ") + std::to_string( uxRequestedSize )).c_str() );
		} // if

		pxSequenceRef = pxReallocRef;
		uxCapacity = uxRequestedSize;
	} // method
	
public :
	PlainSequence() 
	{
		vResetProtectedAttributes();
	} // default constructor

	virtual ~PlainSequence()
	{
		/* Release all allocated memory.
		 */
		vReleaseMemory();
	} // destructor

	/** This moves the ownership of the protected attributes to another object.
	 * The receiver of pxSequenceRef is responsible for its deletion.
	 */
	void vTransferOwnership( PlainSequence &rReceivingSequence )
	{
		/* We transport the three protected attributes to the receiver ...
		 */
		rReceivingSequence.pxSequenceRef = this->pxSequenceRef;
		rReceivingSequence.uiSize = this->uiSize;
		rReceivingSequence.uxCapacity = this->uxCapacity;

		/* ... and delete the knowledge here
		 */
		vResetProtectedAttributes();
	} // protected method

	/** Clears the inner sequence, but does not deallocate the memory.
	 */
	inline void vClear()
	{
		uiSize = 0;
	} // method

	/** Returns whether the sequence is empty or not.
	 */
	inline bool bEmpty()
	{
		return uiSize == 0;
	} // method

	inline bool empty() const
	{
		return uiSize == 0;
	} // method

	/** Fast getter and setter for element access.
	 * If assertions activated we do a range check.
	 */
	inline ELEMENT_TYPE operator[]( size_t uiSubscript ) const
	{
		assert( uiSubscript < uiSize );
		return pxSequenceRef[uiSubscript];
	} // method (get)
	inline ELEMENT_TYPE & operator[]( size_t uiSubscript )
	{
		assert( uiSubscript < uiSize );
		return pxSequenceRef[uiSubscript];
	} // method (set)

	/** Resizes the internal buffer of the sequence to the requested value.
	 */
	inline void resize( size_t uiRequestedSize ) // throws exception
	{	/* Check, whether we have enough capacity, if not reserve memory
		 */
		if ( uxCapacity < uiRequestedSize )
		{
			vReserveMemory( uiRequestedSize );
		} // if
		
		uiSize = uiRequestedSize;
	} // method

	/** Because we want the reference to the sequence private we offer a getter method.
	 * WARNING! Here you can get a null-pointer.
	 */
	inline const ELEMENT_TYPE* const pGetSequenceRef() const
	{
		return this->pxSequenceRef;
	} // method

	/** Because we want to keep the size private we offer a getter method.
	 */
	inline const size_t uxGetSequenceSize() const
	{
		return this->uiSize;
	} // method

	inline const size_t length() const
	{
		return this->uiSize;
	} // method

	/** Reverse the elements of the plain sequence.
	 */
	inline void vReverse()
	{
		reverse( pxSequenceRef, uiSize );
	} // method
	
	/** WARNING: the inner string might not null-terminated after this operation.
	 */
	inline PlainSequence& vAppend( const ELEMENT_TYPE* pSequence, size_t uxNumberOfElements )
	{
		size_t uxRequestedSize = uxNumberOfElements + this->uiSize;

		if ( uxCapacity < uxRequestedSize )
		{
			vReserveMemory ( uxRequestedSize );
		} // if

		/** WARNING: If we work later with non 8-bit data we have to be careful here
		 */
		memcpy( this->pxSequenceRef + uiSize, pSequence, uxNumberOfElements * sizeof(ELEMENT_TYPE) );

		uiSize = uxRequestedSize;

		return *this;
	} // method

	/** Push back of a single symbol.
	 */
	inline void push_back( const ELEMENT_TYPE xElement )
	{
		if ( this->uiSize >= this->uxCapacity )
		{
			vReserveMemory( this->uiSize + 1 );
		} // if

		pxSequenceRef[uiSize + 1] = xElement;
		uiSize++;
	} // method

	/** Compares two sequences for equality
	 */
	inline bool equal(const PlainSequence &rOtherSequence)
	{
		if ( this->uiSize == rOtherSequence.uiSize )
		{
			return memcmp(this->pxSequenceRef, rOtherSequence.pxSequenceRef, sizeof(ELEMENT_TYPE) * uiSize ) == 0;
		} // if
		
		return false;
	} // method
}; // class PlainSequence

/** 
 * @brief a sequence of chars.
 * @details
 * This call was exclusively build for the fasta-reader.
 * It shall boost performance for long inputs.
 */
class TextSequence : public PlainSequence<char>
{
public :
       TextSequence() : PlainSequence<char>()
       {
       } // default constructor

       TextSequence( const char* pcString ) : PlainSequence<char>()
       {
               vAppend(pcString );
       } // text constructor

       /** Terminates the inner string in the C-style using a null-character and
        * returns a reference to the location of the inner string.
        */
       inline char* cString()
       {
               if ( uxCapacity < this->uiSize + 1 )
               {
                       vReserveMemory ( this->uiSize + 1 );
               } // if

               this->pxSequenceRef[this->uiSize] = '\0';

               return pxSequenceRef;
       } // method

       inline void vAppend( const char &pcChar )
       {
               if ( uxCapacity < ( this->uiSize + 1 ) )
               {
                       vReserveMemory ( this->uiSize + 1 );
               } // if

               this->pxSequenceRef[this->uiSize] = pcChar;
               this->uiSize++;
       } // method

       /* Appends the content of pcString to the current buffer
        */
       inline void vAppend( const char* pcString )
       {
               PlainSequence<char>::vAppend( pcString, strlen( pcString ) );
       } // method
}; // class


/** 
 * @brief Special Class for Genetic Sequences
 * @details
 * IDEA: BioSequence objects use numbers instead of characters for sequence representation
 * Supports:
 *  - translation from textual representation to representation as sequence of numbers.
 *  - generation of the reverse strand 
 */

class GeneticSequence : public PlainSequence<uint8_t>
{
public :
	/** The type of elements represented by our sequence.
	 * (the type has to be decided in the context of the construction)
	 */
	// const SequenceType eContentType; 

	/* Disable the copy constructor.
	 */
	GeneticSequence( const GeneticSequence& ) = delete;

	GeneticSequence( ) // : eContentType( SEQUENCE_IS_NUCLEOTIDE )
	{
	} // default constructor

	GeneticSequence (GeneticSequence && g) // : eContentType( SEQUENCE_IS_NUCLEOTIDE )
	{
	} // default constructor

	/* TO DO: Make the 5 a class constant!
	 */
	inline uint8_t uxAlphabetSize() const
	{
		/* The Alphabet size for sequences of nucleotides is 5.
		 */
		return 5; // eContentType == SEQUENCE_IS_NUCLEOTIDE ? 5 : 20;
	} // method
}; // class

/** 
 * @brief Contains a genetic sequence made out of nucleotides (A, C, G, T).
 * @details
 * Class for genetic sequence that consist of nucleotides. (A, C, G, T)
 * @ingroup container
 */
class NucleotideSequence : public GeneticSequence, public Container
{
private :

public :
	/** The table used to translate from base pairs to numeric codes for nucleotides
	 */
	static const unsigned char xNucleotideTranslationTable[256];

	/** Default constructor
	 */
	NucleotideSequence()
		: GeneticSequence()
	{ } // default constructor

	/** Constructor that get the initial content of the sequence in text form.
	 * FIX ME: This can be done a bit more efficient via the GeneticSequence class.
	 */
	NucleotideSequence( const std::string &rsInitialText )
		: GeneticSequence()
	{
		vAppend( rsInitialText.c_str() );
	} // constructor

	/** Move constructor on the foundation of text sequences.
	* Reuses the space of the text-sequence! TO DO: move & to &&
	*/
	NucleotideSequence( TextSequence &rSequence )
	{
			/* We strip the given sequence of its content and move it to our new sequence.
			* WARNING: Here we assume that the sizes for the types char and uint8_t are equal.
			*/
			rSequence.vTransferOwnership( (PlainSequence<char>&)(*this) );

			/* The given PlainSequence should be in textual, we have to translate it.
			*/
			vTranslateToNumericFormUsingTable( xNucleotideTranslationTable, 0 );
	} // constructor


	/** is implicitly deleted by geneticSequence but boost python needs to know */
	NucleotideSequence(const NucleotideSequence&) = delete;

	/** used to identify the nucleotide sequence datatype in the aligner pipeline*/
    ContainerType getType(){return ContainerType::nucSeq;}

	/** Delivers the complement of a single nucleotide.
	 */
	static inline char nucleotideComplement( char iNucleotide )
	{
		/* Complements of nucleotides
		 *							   0  1  2  3
		 */
		static const char chars[4] = { 3, 2, 1, 0 };

		return ( iNucleotide < 4 ) ? chars[(int)iNucleotide] : 5;
	} // static method

	/** Iterates over all base pairs in the sequence and creates the complement. 
	 * (A -> T, T -> A, C -> G, G -> C)
	 */
	void vSwitchAllBasePairsToComplement()
	{
		for( size_t uxIterator = 0; uxIterator < uiSize; uxIterator++ )
		{
			pxSequenceRef[uxIterator] = nucleotideComplement( pxSequenceRef[uxIterator] );
		} // for
	} // function

	/** transforms the character representation into a representation on the foundation of digits.
	 */
	void vTranslateToNumericFormUsingTable( const unsigned char *alphabetTranslationTable,
											size_t uxStartIndex
										  )
	{
		for( size_t uxIterator = uxStartIndex; uxIterator < uiSize; uxIterator++ )
		{
			pxSequenceRef[uxIterator] = alphabetTranslationTable[ pxSequenceRef[uxIterator] ];
		} // for
	} // method

	/** Gives the textual representation for some numeric representation.
	 * Important: Keep this inline, so that it is not compiled into a function of its own. 
	 */
	static inline char translateACGTCodeToCharacter( uint8_t uiNucleotideCode )
	{
		static const char chars[4] = {'A', 'C', 'G', 'T'};
		if (uiNucleotideCode < 4)
		{
			return chars[uiNucleotideCode];
		} // if
		else
		{
			return 'N';
		} // else
	} // static method

	/** The symbol on some position in textual form.
	 * We count starting from 0.
	 */
	inline char charAt( size_t uxPosition )
	{
		if ( uxPosition >= uiSize)
		{
			throw fasta_reader_exception("Index out of range");
		} // if

		return translateACGTCodeToCharacter( pxSequenceRef[uxPosition] );
	} // method

	/** Appends a string containing nucleotides as text and automatically translates the symbols.
	 */
	void vAppend( const char* pcString )
	{
		size_t uxSizeBeforeAppendOperation = this->uiSize;
		
		/* WARNING! char and uint8_t must have the same size or we get a serious problem here!
		 */
		PlainSequence<uint8_t>::vAppend( (const uint8_t*)pcString, strlen( pcString ) );

		vTranslateToNumericFormUsingTable( xNucleotideTranslationTable, uxSizeBeforeAppendOperation );
	} // method

	/** wrapper for boost
	 */
	void vAppend_boost( const char* pcString )
	{
		vAppend(pcString);
	} // method
	
	
	std::string toString()
	{
		std::string ret = "";
		for (unsigned int i = 0; i < length(); i++)
				ret += charAt(i);
		return ret;
	}//function
	
}; // class NucleotideSequence

/**
 * @brief export this module to boost python 
 * @ingroup export
 */
void exportSequence();