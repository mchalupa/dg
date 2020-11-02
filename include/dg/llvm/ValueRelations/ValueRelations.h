#ifndef DG_LLVM_RELATIONS_MAP_H_
#define DG_LLVM_RELATIONS_MAP_H_

#include <set>
#include <map>
#include <stack>
#include <vector>
#include <tuple>
#include <memory>
#include <algorithm>

#include <cassert>

#include <llvm/IR/Value.h>
#include <llvm/IR/Constants.h>

#ifndef NDEBUG
    #include <iostream>
	#include "getValName.h"
#endif

namespace {

template <typename Key, typename Val>
bool contains(const std::map<Key, Val>& map, const Key& key) {
	return map.find(key) != map.end();
}

template <typename Val>
bool contains(const std::set<Val>& set, const Val& val) {
	return set.find(val) != set.end();
}

template <typename T>
void eraseUniquePtr(std::vector<std::unique_ptr<T>>& set, const T* const value) {
	auto ite = std::find_if(set.begin(), set.end(),
                            [&value](std::unique_ptr<T>& ptr) -> bool {
                                return ptr.get() == value;
                             });
	assert(ite != set.end());
	set.erase(ite);
}

template <typename T>
void substitueInSet(const std::map<T, T>& mapping, std::set<T>& set) {
	std::set<T> newSet;

	for (auto& element : set) {
		if (contains(mapping, element))
			newSet.insert(mapping.at(element));
		else
			newSet.insert(element);
	}
	set.swap(newSet);
}

template <typename T>
T findByKey(const std::map<T, T>& map, T key) {
	auto found = map.find(key);
	if (found == map.end())
		return nullptr;
	return found->second;
}

template <typename T>
T findByValue(const std::map<T, T>& map, T value) {
	for (auto& pair : map) {
		if (pair.second == value)
			return pair.first;
	}
	return nullptr;
}

} // namespace

namespace dg {
namespace vr {

class EqualityBucket;

using BucketPtr = EqualityBucket*;
using BucketPtrSet = std::set<BucketPtr>;

enum class Relation { EQ, NE, LE, LT, GE, GT, LOAD };

#ifndef NDEBUG
void dumpRelation(Relation r) {
	switch(r) {
		case Relation::EQ: std::cerr << "EQ";
						   break;
		case Relation::NE: std::cerr << "NE";
						   break;
		case Relation::LE: std::cerr << "LE";
						   break;
		case Relation::LT: std::cerr << "LT";
						   break;
		case Relation::GE: std::cerr << "GE";
						   break;
		case Relation::GT: std::cerr << "GT";
						   break;
		case Relation::LOAD: std::cerr << "LOAD";
						     break;
	}
}
#endif

class EqualityBucket {

	using T = const llvm::Value*;

    friend class ValueRelations;

    using BucketPtr = EqualityBucket*;
	using BucketPtrSet = std::set<BucketPtr>;
	
	BucketPtrSet lesserEqual;
	BucketPtrSet lesser;
	BucketPtrSet parents;

	std::vector<T> equalities;

	struct RelatedBucket {
		EqualityBucket* bucket;
		Relation relation;

		RelatedBucket(): bucket(nullptr), relation(Relation::EQ) {}

		RelatedBucket(EqualityBucket* b, Relation r): bucket(b), relation(r) {}

		friend bool operator==(const RelatedBucket& lt, const RelatedBucket& rt) {
			return lt.bucket == rt.bucket && lt.relation == rt.relation;
		}

		friend bool operator!=(const RelatedBucket& lt, const RelatedBucket& rt) {
			return !(lt == rt);
		}
	};

	class BucketIterator {

		using value_type = RelatedBucket;
		bool goDown = false;
		bool toFirstStrict = false;

		std::vector<EqualityBucket*> strictlyRelated;
		std::vector<EqualityBucket*> nonStrictlyRelated;

		EqualityBucket* start;
		unsigned index = 0;
		bool inStrict = true;

		RelatedBucket current;

		void computeSetsForStrict(
				EqualityBucket* strictlyRelated,
				std::set<EqualityBucket*>& strict,
				const std::map<EqualityBucket*, BucketPtrSet>& nonStrictlyRelatedMap,
				const std::map<EqualityBucket*, BucketPtrSet>& strictlyRelatedMap) {

			auto& succNonStrict = nonStrictlyRelatedMap.at(strictlyRelated);
			if (!toFirstStrict)
				strict.insert(succNonStrict.begin(), succNonStrict.end());

			auto& succStrict = strictlyRelatedMap.at(strictlyRelated);
			strict.insert(succStrict.begin(), succStrict.end());

			strict.emplace(strictlyRelated);
		}

		void computeSetsForNonStrict(
				EqualityBucket* nonStrictlyRelated,
				std::set<EqualityBucket*>& nonStrict,
				std::set<EqualityBucket*>& strict,
				const std::map<EqualityBucket*, BucketPtrSet>& nonStrictlyRelatedMap) {

			// stop if the bucket is already considered strictly related
			if (strict.find(nonStrictlyRelated) != strict.end())
				return;

			for (EqualityBucket* nonStrictRelated : nonStrictlyRelatedMap.at(nonStrictlyRelated)) {
				// add nonStrict related bucket only if it is not already considered strictly related
				if (strict.find(nonStrictRelated) == strict.end())
					nonStrict.emplace(nonStrictRelated);
			}
			nonStrict.emplace(nonStrictlyRelated);
		}

		void computeRelated() {
			std::map<EqualityBucket*, BucketPtrSet> strictlyRelatedMap;
			std::map<EqualityBucket*, BucketPtrSet> nonStrictlyRelatedMap;

			BucketPtrSet visited;
			std::stack<DFSFrame> stack;

			stack.emplace(start, goDown ? start->lesser.begin() : start->parents.begin());
			visited.emplace(start);

			while (!stack.empty()) {
				DFSFrame frame = stack.top();
				stack.pop();

				if (goDown && frame.it == frame.bucket->lesser.end())
					frame.it = frame.bucket->lesserEqual.begin();

				if ((goDown && frame.it == frame.bucket->lesserEqual.end()) 
						|| (!goDown && frame.it == frame.bucket->parents.end())) {
					// in post order compute the set of strictly related buckets
					// from the sets in its successors
					auto itNonStrict = nonStrictlyRelatedMap.emplace(frame.bucket, std::set<EqualityBucket*>()).first;
					auto itStrict = strictlyRelatedMap.emplace(frame.bucket, std::set<EqualityBucket*>()).first;
					std::set<EqualityBucket*>& nonStrict = itNonStrict->second;
					std::set<EqualityBucket*>& strict = itStrict->second;

					if (goDown) {
						for (EqualityBucket* lesser : frame.bucket->lesser)
							computeSetsForStrict(lesser, strict, nonStrictlyRelatedMap, strictlyRelatedMap);

						for (EqualityBucket* lesserEqual : frame.bucket->lesserEqual) {
							auto& strictSucc = strictlyRelatedMap[lesserEqual];
							strict.insert(strictSucc.begin(), strictSucc.end());
						}

						for (EqualityBucket* lesserEqual : frame.bucket->lesserEqual)
							computeSetsForNonStrict(lesserEqual, nonStrict, strict, nonStrictlyRelatedMap);

					} else {
						for (EqualityBucket* parent : frame.bucket->parents) {
							if (parent->lesser.find(frame.bucket) != parent->lesser.end())
								computeSetsForStrict(parent, strict, nonStrictlyRelatedMap, strictlyRelatedMap);
							else { // else this bucket is lesserEqual to parent
								auto& strictSucc = strictlyRelatedMap[parent];
								strict.insert(strictSucc.begin(), strictSucc.end());
							}
						}

						for (EqualityBucket* parent : frame.bucket->parents) {
							if (parent->lesserEqual.find(frame.bucket) != parent->lesserEqual.end())
								computeSetsForNonStrict(parent, nonStrict, strict, nonStrictlyRelatedMap);
						}
					}

					continue;
				}

				// plan visit for the next successor
				stack.emplace(frame.bucket, std::next(frame.it));

				// plan visit to the current successor
				if (visited.find(*frame.it) == visited.end()) {
					visited.emplace(*frame.it);
					if (goDown)
						stack.emplace(*frame.it, (*frame.it)->lesser.begin());
					else
						stack.emplace(*frame.it, (*frame.it)->parents.begin());
				}
			}
			const auto& strictSet = strictlyRelatedMap[start];
			strictlyRelated.insert(strictlyRelated.end(), strictSet.begin(), strictSet.end());

			const auto& nonStrictSet = nonStrictlyRelatedMap[start];
			nonStrictlyRelated.insert(nonStrictlyRelated.end(), nonStrictSet.begin(), nonStrictSet.end());
		}

public:

		struct DFSFrame {
			EqualityBucket* bucket;
			typename BucketPtrSet::iterator it;

			DFSFrame(EqualityBucket* b, typename BucketPtrSet::iterator i)
				: bucket(b), it(i) {}
		};

		BucketIterator() = default;
		BucketIterator(EqualityBucket* s, bool down, bool strict, bool begin)
		: goDown(down), toFirstStrict(strict), start(s), current(start, Relation::EQ) {
			if (!begin) {
				current = RelatedBucket(nullptr, Relation::EQ);
				return;
			}

			computeRelated();
		}

		friend bool operator==(const BucketIterator& lt, const BucketIterator& rt) {
			return lt.goDown == rt.goDown
				&& lt.toFirstStrict == rt.toFirstStrict
				&& lt.current == rt.current;
		}

		friend bool operator!=(const BucketIterator& lt, const BucketIterator& rt) {
			return !(lt == rt);
		}

		const value_type& operator*() const { return current; }
		const value_type* operator->() const { return &current; }

		value_type operator*() { return current; }
		value_type* operator->() { return &current; }

		BucketIterator& operator++() {

			if (inStrict && index >= strictlyRelated.size()) {
				index = 0;
				inStrict = false;
			}

			if (!inStrict && index >= nonStrictlyRelated.size()) {
				current = RelatedBucket(nullptr, Relation::EQ);
				return *this;
			}

			if (inStrict)
				current = RelatedBucket(strictlyRelated[index], goDown ? Relation::LT : Relation::GT);
			else
				current = RelatedBucket(nonStrictlyRelated[index], goDown ? Relation::LE : Relation::GE);

			++index;
			return *this;
		}

		BucketIterator operator++(int) {
			auto preInc = *this;
			++(*this);
			return preInc;
		}

	};

	BucketIterator begin_down() {
		return BucketIterator(this, true, false, true);
	}

	BucketIterator end_down() {
		return BucketIterator(this, true, false, false);
	}

	BucketIterator begin_up() {
		return BucketIterator(this, false, false, true);
	}

	BucketIterator end_up() {
		return BucketIterator(this, false, false, false);
	}

	// iterates over buckets up to the first strictly lesser on each path
	BucketIterator begin_strictDown() {
		return BucketIterator(this, true, true, true);
	}

	BucketIterator end_strictDown() {
		return BucketIterator(this, true, true, false);
	}

	// iterates over buckets up to the first strictly greater on each path
	BucketIterator begin_strictUp() {
		return BucketIterator(this, false, true, true);
	}

	BucketIterator end_strictUp() {
		return BucketIterator(this, false, true, false);
	}

	bool subtreeContains(
			EqualityBucket* needle,
			bool ignoreLE) {

		for (auto it = begin_down(); it != end_down(); ++it) {

			if (it->bucket == needle) {
				if (ignoreLE && it->relation != Relation::LT)
					break;
				// else we found searched bucket
				return true;
			}
		}
		return false;
	}

	// return path from bucket to this
	std::vector<EqualityBucket*> getLesserEqualPath(EqualityBucket* bucket) {
		std::vector<EqualityBucket*> result;

		BucketPtrSet visited;
		std::stack<BucketIterator::DFSFrame> stack;

		stack.emplace(this, lesserEqual.begin());
		visited.emplace(this);

		while (!stack.empty()) {
			BucketIterator::DFSFrame frame = stack.top();
			stack.pop();

			// if we found the searched bucket, reconstruct the path and return
			if (frame.bucket == bucket) {
				std::vector<EqualityBucket*> result;
				result.emplace_back(bucket);

				while (!stack.empty()) {
					frame = stack.top();
					stack.pop();
					result.emplace_back(frame.bucket);
				}
				return result;
			}

			if (frame.it == frame.bucket->lesserEqual.end())
				continue;

			// plan visit for the next successor
			stack.emplace(frame.bucket, std::next(frame.it));

			// plan visit to the current successor
			if (visited.find(*frame.it) == visited.end()) {
				visited.emplace(*frame.it);
				stack.emplace(*frame.it, (*frame.it)->lesserEqual.begin());
			}
		}

		assert(0 && "unreachable");
		abort();
	}

	void mergeConnections(const EqualityBucket& other) {
		// set_union does't work in place
		lesserEqual.insert(other.lesserEqual.begin(), other.lesserEqual.end());
		for (EqualityBucket* bucketPtr : other.lesserEqual)
			bucketPtr->parents.insert(this);

		lesser.insert(other.lesser.begin(), other.lesser.end());
		for (EqualityBucket* bucketPtr : other.lesser)
			bucketPtr->parents.insert(this);

		parents.insert(other.parents.begin(), other.parents.end());
		for (EqualityBucket* parent : other.parents) {
			if (contains(parent->lesserEqual, const_cast<EqualityBucket*>(&other)))
				parent->lesserEqual.insert(this);
			else if (contains(parent->lesser, const_cast<EqualityBucket*>(&other)))
				parent->lesser.insert(this);
			else
				assert(0); // was a parent so it must have been lesser or lesserEqual
		}

		equalities.insert(equalities.end(),
						  other.equalities.begin(), other.equalities.end());
	}

	void disconnectAll() {
		for (auto* parent : parents) {
			parent->lesserEqual.erase(this);
			parent->lesser.erase(this);
		}
		parents.clear();

		for (auto* bucketPtr : lesserEqual) {
			bucketPtr->parents.erase(this);
		}
		lesserEqual.clear();
		
		for (auto* bucketPtr : lesser) {
			bucketPtr->parents.erase(this);
		}
		lesser.clear();
	}

	void substitueAll(const std::map<EqualityBucket*, EqualityBucket*>& oldToNewPtr) {
		substitueInSet<EqualityBucket*>(oldToNewPtr, lesserEqual);
		substitueInSet<EqualityBucket*>(oldToNewPtr, lesser);
		substitueInSet<EqualityBucket*>(oldToNewPtr, parents);
	}

	std::vector<EqualityBucket*> getDirectlyRelated(bool goDown) {
		std::vector<EqualityBucket*> result;

		for (auto it = (goDown ? begin_strictDown() : begin_strictUp());
				  it != (goDown ? end_strictDown() : end_strictUp());
				  ++it) {

			if ((goDown && it->relation == Relation::LT) || (!goDown && it->relation == Relation::GT))
				result.emplace_back(it->bucket);
		}

		return result;
	}

	std::vector<T>& getEqual() {
		return equalities;
	}

	const std::vector<T>& getEqual() const {
		return equalities;
	}

	T getAny() const {
		assert(equalities.size() > 0);
		return equalities[0];
	}

	bool hasAllEqualitiesFrom(const EqualityBucket* other) const {
		for (T val : other->equalities) {
			if (std::find(equalities.begin(), equalities.end(), val) == equalities.end())
				return false;
		}
		return true;
	}
};

class ValueRelations {

	using T = const llvm::Value*;
	using C = const llvm::ConstantInt*;

private:
    std::vector<std::unique_ptr<EqualityBucket>> buckets;
	std::map<T, EqualityBucket*> mapToBucket;
	std::map<unsigned, EqualityBucket*> placeholderBuckets;
	unsigned lastPlaceholderId = 0;

	std::map<EqualityBucket*, std::set<EqualityBucket*>> nonEqualities;

	// map of pairs (a, b) such that {any of b} = load {any of a}
	std::map<EqualityBucket*, EqualityBucket*> loads;

	std::vector<bool> validAreas;

	struct ValueIterator {
		using value_type = std::pair<T, Relation>;

		enum Type { UP, DOWN, ALL, NONE };

		Type type = Type::NONE;
		bool strictOnly = false;
		EqualityBucket* start;
		EqualityBucket::BucketIterator it;
		unsigned index;
		
		ValueIterator(EqualityBucket* st, bool s, Type t, bool begin)
		: type(t), strictOnly(s), start(st), index(0) {
			if (begin) {
				if (type == Type::DOWN || type == Type::ALL)
					it = start->begin_down();
				if (type == Type::UP)
					it = start->begin_up();
				toNextValidValue();
			} else {
				if (type == Type::DOWN)
					it = start->end_down();
				if (type == Type::UP || type == Type::ALL)
					it = start->end_up();
			}
		}

		friend bool operator==(const ValueIterator& lt, const ValueIterator& rt) {
			return lt.type == rt.type
			    && lt.strictOnly == rt.strictOnly
				&& lt.it == rt.it;
		}

		friend bool operator!=(const ValueIterator& lt, const ValueIterator& rt) {
			return !(lt == rt);
		}

		value_type operator*() const {
			if (strictOnly && it->relation != Relation::LT && it->relation != Relation::GT)
				assert(0 && "iterator always stops only at strict if demanded");
			return { it->bucket->getEqual()[index], it->relation };
		}

		// make iterator always point at valid value or end
		ValueIterator& operator++() {
			if (it == start->end_up() || it == start->end_down())
				return *this;
			// we dont have to check if type == ALL because code later
			// handles the jump between iterators

			if (index + 1 < it->bucket->equalities.size()) {
				++index;
				return *this;
			}

			// else we need to move on to the next bucket
			++it;
			index = 0;
			toNextValidValue();

			if (it == start->end_down() && type == Type::ALL) {
				it = ++(start->begin_up()); // ++ so that we would not pass equal again
				toNextValidValue();
			}

			return *this;
		}

		ValueIterator operator++(int) {
			auto preInc = *this;
			++(*this);
			return preInc;
		}
		
	private:
		void toNextValidValue() {
			while (it != start->end_down()
				&& it != start->end_up()
				&& (it->bucket->getEqual().empty()
					|| (strictOnly && it->relation != Relation::LT && it->relation != Relation::GT)))
				++it;
		}
	};

	bool inGraph(T val) const {
		return contains(mapToBucket, val);
	}

	bool inGraph(EqualityBucket* bucket) const {
		return bucket;
	}

	bool hasComparativeRelations(EqualityBucket* bucket) const {
		return bucket->getEqual().size() > 1
			|| nonEqualities.find(bucket) != nonEqualities.end()
			|| ++bucket->begin_down() != bucket->end_down()
			|| ++bucket->begin_up()   != bucket->end_up();
	}

	bool hasComparativeRelationsOrLoads(EqualityBucket* bucket) const {
		return hasComparativeRelations(bucket)
			|| findByKey(loads, bucket)
			|| findByValue(loads, bucket);
	}

	EqualityBucket* getCorrespondingBucket(const ValueRelations& other, EqualityBucket* otherBucket) const {
		if (!otherBucket->getEqual().empty()) {
			auto found = mapToBucket.find(otherBucket->getEqual()[0]);
			if (found != mapToBucket.end())
				return found->second;
			return nullptr;
		}

		// else this is placeholder bucket
		EqualityBucket* otherFromBucket = findByValue(other.loads, otherBucket);
		assert(otherFromBucket);
		assert(!otherFromBucket->getEqual().empty());
		// if bucket is empty, it surely has a nonempty load bucket,
		// they aren't created under different circumstances

		T from = otherFromBucket->getEqual()[0];
		if (hasLoad(from))
			return loads.at(mapToBucket.at(from));
		return nullptr;
	}

	EqualityBucket* getCorrespondingBucketOrNew(const ValueRelations& other, EqualityBucket* otherBucket) {
		if (!otherBucket->getEqual().empty()) {
			const auto& equalities = otherBucket->getEqual();

			for (T val : equalities) {
				auto found = mapToBucket.find(val);
				if (found != mapToBucket.end())
					return found->second;
			}
			add(equalities[0]);
			return mapToBucket.find(equalities[0])->second;
		}

		// else this is placeholder bucket
		EqualityBucket* otherFromBucket = findByValue(other.loads, otherBucket);
		assert(otherFromBucket);
		assert(!otherFromBucket->getEqual().empty());
		// if bucket is empty, it surely has a nonempty load bucket,
		// they aren't created under different circumstances

		for (T from : otherFromBucket->getEqual()) {
			if (hasLoad(from))
				return loads[mapToBucket[from]];
		}
		unsigned placeholder = newPlaceholderBucket();
		setLoad(otherFromBucket->getEqual()[0], placeholder);
		return placeholderBuckets[placeholder];
	}

	std::vector<std::tuple<EqualityBucket*, EqualityBucket*, Relation>>
			getExtraRelationsIn(const ValueRelations& other) const {
		std::vector<std::tuple<EqualityBucket*, EqualityBucket*, Relation>> result;

		for (auto& bucketUniquePtr : other.buckets) {

			EqualityBucket* otherBucket = bucketUniquePtr.get();
			EqualityBucket* thisBucket = getCorrespondingBucket(other, otherBucket);

			if (!thisBucket || !thisBucket->hasAllEqualitiesFrom(otherBucket))
				result.emplace_back(otherBucket, otherBucket, Relation::EQ);
			
			// find unrelated comparative buckets
			for (auto it = otherBucket->begin_down(); it != otherBucket->end_down(); ++it) {

				if (it->relation == Relation::EQ)
					continue; // already handled prior to loop

				EqualityBucket* otherRelatedBucket = it->bucket;
				EqualityBucket* thisRelatedBucket = getCorrespondingBucket(other, otherRelatedBucket);

				if (!thisBucket
				 || !thisRelatedBucket
				 || (it->relation == Relation::LT && !isLesser(thisRelatedBucket, thisBucket))
				 || (it->relation == Relation::LE && !isLesserEqual(thisRelatedBucket, thisBucket)))
					result.emplace_back(otherRelatedBucket, otherBucket, it->relation);
			}

			// find urelated non-equal buckets
			auto foundNE = other.nonEqualities.find(otherBucket);
			if (foundNE != other.nonEqualities.end()) {
				for (EqualityBucket* otherRelatedBucket : foundNE->second) {
					EqualityBucket* thisRelatedBucket = getCorrespondingBucket(other, otherRelatedBucket);

					if (!thisBucket
					 || !thisRelatedBucket
					 || !isNonEqual(thisRelatedBucket, thisBucket))
						result.emplace_back(otherRelatedBucket, otherBucket, Relation::NE);
				}
			}

			// found unrelated load buckets
			auto foundLoad = other.loads.find(otherBucket);
			if (foundLoad != other.loads.end()) {
				EqualityBucket* otherRelatedBucket = foundLoad->second;
				EqualityBucket* thisRelatedBucket = getCorrespondingBucket(other, otherRelatedBucket);

				if (!thisBucket
				 || !thisRelatedBucket
				 || !isLoad(thisBucket, thisRelatedBucket))
				 	result.emplace_back(otherRelatedBucket, otherBucket, Relation::LOAD);
			}
		}

		return result;
	}

	std::vector<BucketPtr> getBucketsToMerge(BucketPtr newBucketPtr, BucketPtr oldBucketPtr) const {

		if (!isLesserEqual(newBucketPtr, oldBucketPtr) && !isLesserEqual(oldBucketPtr, newBucketPtr))
			return { newBucketPtr, oldBucketPtr };

		// else handle lesserEqual specializing to equal
		std::vector<EqualityBucket*> toMerge;
		if (isLesserEqual(newBucketPtr, oldBucketPtr)) {
			toMerge = oldBucketPtr->getLesserEqualPath(newBucketPtr);
		} else {
			toMerge = newBucketPtr->getLesserEqualPath(oldBucketPtr);
		}

		// unset unnecessary lesserEqual relations
		for (auto it = ++toMerge.begin(); it != toMerge.end(); ++it) {
			EqualityBucket* below = *std::prev(it);
			EqualityBucket* above = *it;

			above->lesserEqual.erase(below);
			below->parents.erase(above);
		}

		return toMerge;
	}

	void setEqual(EqualityBucket* newBucketPtr, EqualityBucket* oldBucketPtr) {

		if (isEqual(newBucketPtr, oldBucketPtr))
			return;

		assert(!hasConflictingRelation(newBucketPtr, oldBucketPtr, Relation::EQ));

		std::vector<BucketPtr> toMerge = getBucketsToMerge(newBucketPtr, oldBucketPtr);

		newBucketPtr = toMerge[0];

		for (auto it = ++toMerge.begin(); it != toMerge.end(); ++it) {

			oldBucketPtr = *it;

			// replace nonEquality info to regard only remaining bucket
			auto newNEIt = nonEqualities.find(newBucketPtr);
			auto oldNEIt = nonEqualities.find(oldBucketPtr);

			if (oldNEIt != nonEqualities.end()) {
				for (EqualityBucket* nonEqual : oldNEIt->second) {
					nonEqualities.at(nonEqual).emplace(newBucketPtr);
					nonEqualities.at(nonEqual).erase(oldBucketPtr);
				}

				oldNEIt->second.erase(newBucketPtr);
				if (newNEIt != nonEqualities.end())
					newNEIt->second.insert(oldNEIt->second.begin(), oldNEIt->second.end());
				else
					nonEqualities.emplace(newBucketPtr, oldNEIt->second);

				nonEqualities.erase(oldBucketPtr);
			}

			// replace mapToBucket info to regard only remaining bucket
			for (auto& pair : mapToBucket) {
				if (pair.second == oldBucketPtr)
					pair.second = newBucketPtr;
			}

			// replace load info to regard only remaining bucket
			for (auto pairIt = loads.begin(); pairIt != loads.end(); ++pairIt) {
				if (pairIt->first == oldBucketPtr) {
					loads.emplace(newBucketPtr,
								  pairIt->second == oldBucketPtr ? newBucketPtr : pairIt->second); // in case x = load x
					pairIt = loads.erase(pairIt);
				}

				if (pairIt->second == oldBucketPtr)
					pairIt->second = newBucketPtr;
			}

			// make successors and parents of right belong to left too
			newBucketPtr->mergeConnections(*oldBucketPtr);

			// make successors and parents of right forget it
			oldBucketPtr->disconnectAll();

			// replace placeholder info to disregard removed bucket
			for (auto pair : placeholderBuckets) {
				if (pair.second == oldBucketPtr) {
					placeholderBuckets.erase(pair.first);
					break;
				}
			}

			// remove right
			eraseUniquePtr(buckets, oldBucketPtr);
		}
	}

	void setNonEqual(EqualityBucket* ltBucketPtr, EqualityBucket* rtBucketPtr) {
		
		if (isNonEqual(ltBucketPtr, rtBucketPtr))
			return;

		assert(!hasConflictingRelation(ltBucketPtr, rtBucketPtr, Relation::NE));

		// TODO? handle lesserEqual specializing to lesser

		auto foundLt = nonEqualities.find(ltBucketPtr);
		if (foundLt != nonEqualities.end())
			foundLt->second.emplace(rtBucketPtr);
		else
			nonEqualities.emplace(ltBucketPtr, std::set<EqualityBucket*>{rtBucketPtr});

		auto foundRt = nonEqualities.find(rtBucketPtr);
		if (foundRt != nonEqualities.end())
			foundRt->second.emplace(ltBucketPtr);
		else
			nonEqualities.emplace(rtBucketPtr, std::set<EqualityBucket*>{ltBucketPtr});
	}

	void setLesser(EqualityBucket* ltBucketPtr, EqualityBucket* rtBucketPtr) {
		if (isLesser(ltBucketPtr, rtBucketPtr))
			return;

		assert(!hasConflictingRelation(ltBucketPtr, rtBucketPtr, Relation::LT));

		if (isLesserEqual(ltBucketPtr, rtBucketPtr)) {
			if (contains<EqualityBucket*>(rtBucketPtr->lesserEqual, ltBucketPtr))
				rtBucketPtr->lesserEqual.erase(ltBucketPtr);
			//else
			//	assert(0); // more buckets in between, can't decide this
		}

		rtBucketPtr->lesser.insert(ltBucketPtr);
		ltBucketPtr->parents.insert(rtBucketPtr);
	}

	void setLesserEqual(EqualityBucket* ltBucketPtr, EqualityBucket* rtBucketPtr) {
		if (isLesserEqual(ltBucketPtr, rtBucketPtr))
			return;
		if (isNonEqual(ltBucketPtr, rtBucketPtr))
			return setLesser(ltBucketPtr, rtBucketPtr);

		assert(!hasConflictingRelation(ltBucketPtr, rtBucketPtr, Relation::LE));

		// infer values being equal
		if (isLesserEqual(rtBucketPtr, ltBucketPtr))
			return setEqual(ltBucketPtr, rtBucketPtr);

		rtBucketPtr->lesserEqual.insert(ltBucketPtr);
		ltBucketPtr->parents.insert(rtBucketPtr);
	}

	void setLoad(EqualityBucket* fromBucketPtr, EqualityBucket* valBucketPtr) {
		if (isLoad(fromBucketPtr, valBucketPtr))
			return;

		// get set of values that load from equal pointers
		EqualityBucket* valEqualBucketPtr = findByKey(loads, fromBucketPtr);

		// if there is such a set, we just add val to it
		if (valEqualBucketPtr) {
			setEqual(valBucketPtr, valEqualBucketPtr);
		} else {
			loads.emplace(fromBucketPtr, valBucketPtr);
		}
	}

	bool isEqual(EqualityBucket* ltEqBucket, EqualityBucket* rtEqBucket) const {

		return ltEqBucket == rtEqBucket;
	}

	bool isNonEqual(EqualityBucket* ltEqBucket, EqualityBucket* rtEqBucket) const {
		auto found = nonEqualities.find(ltEqBucket);
		if (found == nonEqualities.end())
			return false;

		return found->second.find(rtEqBucket) != found->second.end();
	}

	C getEqualConstant(EqualityBucket* ltEqBucket) const {
		C ltConst = nullptr;
		for (const llvm::Value* val : ltEqBucket->getEqual()) {
			if (auto constant = llvm::dyn_cast<llvm::ConstantInt>(val))
				ltConst = constant;
		}
		
		return ltConst;
	}

	bool isLesser(EqualityBucket* ltEqBucket, EqualityBucket* rtEqBucket) const {
		return rtEqBucket->subtreeContains(ltEqBucket, true);
	}

	bool isLesserEqual(EqualityBucket* ltEqBucket, EqualityBucket* rtEqBucket) const {
		return rtEqBucket->subtreeContains(ltEqBucket, false);
	}

	// in case of LOAD, rt is the from and lt is val
	bool hasConflictingRelation(
			EqualityBucket* ltBucketPtr,
			EqualityBucket* rtBucketPtr,
			Relation relation) const {
		switch (relation) {
			case Relation::EQ:
				return isNonEqual(ltBucketPtr, rtBucketPtr)
					|| isLesser(ltBucketPtr, rtBucketPtr)
					|| isLesser(rtBucketPtr, ltBucketPtr);

			case Relation::NE:
				return isEqual(ltBucketPtr, rtBucketPtr);

			case Relation::LT:
				return isLesserEqual(rtBucketPtr, ltBucketPtr);

			case Relation::LE:
				return isLesser(rtBucketPtr, ltBucketPtr);

			case Relation::GT:
				return hasConflictingRelation(rtBucketPtr, ltBucketPtr, Relation::LT);

			case Relation::GE:
				return hasConflictingRelation(rtBucketPtr, ltBucketPtr, Relation::LE);

			case Relation::LOAD:
				return hasLoad(rtBucketPtr)
					&& hasConflictingRelation(ltBucketPtr, loads.at(rtBucketPtr), Relation::EQ);
		}
		assert(0 && "unreachable");
		abort();
	}

	bool isLoad(EqualityBucket* fromBucketPtr, EqualityBucket* valBucketPtr) const {
		auto found = loads.find(fromBucketPtr);
		return found != loads.end() && valBucketPtr == found->second;
	}

	bool hasLoad(EqualityBucket* fromBucketPtr) const {
		return loads.find(fromBucketPtr) != loads.end();
	}

	void eraseBucketIfUnrelated(EqualityBucket* bucket) {
		if (hasComparativeRelationsOrLoads(bucket))
			return;

		for (auto& pair : mapToBucket) {
			if (pair.second == bucket) {
				mapToBucket.erase(pair.first);
				break;
			}
		}

		eraseUniquePtr(buckets, bucket);
	}

	void unsetComparativeRelations(EqualityBucket* valBucketPtr) {
		// save related buckets to check later
		BucketPtrSet allRelated;
		allRelated.insert(valBucketPtr->parents.begin(), valBucketPtr->parents.end());
		allRelated.insert(valBucketPtr->lesser.begin(), valBucketPtr->lesser.end());
		allRelated.insert(valBucketPtr->lesserEqual.begin(), valBucketPtr->lesserEqual.end());

		auto found = nonEqualities.find(valBucketPtr);
		if (found != nonEqualities.end())
			allRelated.insert(found->second.begin(), found->second.end());

		// overconnect parents to children
		for (EqualityBucket* parent : valBucketPtr->parents) {

			for (EqualityBucket* lesser : valBucketPtr->lesser) {
				parent->lesser.emplace(lesser);
				lesser->parents.emplace(parent);
			}

			for (EqualityBucket* lesserEqual : valBucketPtr->lesserEqual) {

				if (parent->lesserEqual.find(valBucketPtr) != parent->lesserEqual.end())
					parent->lesserEqual.emplace(lesserEqual);
				else
					parent->lesser.emplace(lesserEqual);
				lesserEqual->parents.emplace(parent);
			}
		}

		nonEqualities.erase(valBucketPtr);
		for (auto& pair : nonEqualities) {
			pair.second.erase(valBucketPtr);
		}

		// it severes all ties with the rest of the graph
		valBucketPtr->disconnectAll();

		// remove buckets that lost their only relation
		for (EqualityBucket* bucket : allRelated)
			eraseBucketIfUnrelated(bucket);
	}

	C getLowerBound(EqualityBucket* bucket, bool strict) const {

		C highest = nullptr;
		for (auto it = bucket->begin_down(); it != bucket->end_down(); ++it) {
			C constant = getEqualConstant(it->bucket);

			if ((!strict || it->relation == Relation::LT) // ignore strict values if demanded
			 && (!highest || (constant && constant->getSExtValue() > highest->getSExtValue())))
				highest = constant;
		}
		return highest;
	}

	C getUpperBound(EqualityBucket* bucket, bool strict) const {

		C lowest = nullptr;
		for (auto it = bucket->begin_up(); it != bucket->end_up(); ++it) {
			C constant = getEqualConstant(it->bucket);

			if ((!strict || it->relation == Relation::GT)
			 && (!lowest || (constant && constant->getSExtValue() < lowest->getSExtValue())))
				lowest = constant;
		}
		return lowest;
	}

	std::vector<T> getDirectlyRelated(T val, bool goDown) const {
		if (!inGraph(val))
			return {};
		EqualityBucket* bucketPtr = mapToBucket.at(val);

		std::vector<EqualityBucket*> relatedBuckets = bucketPtr->getDirectlyRelated(goDown);

		std::vector<T> result;
		for (EqualityBucket* bucketPtr : relatedBuckets) {
			if (!bucketPtr->getEqual().empty())
				result.emplace_back(bucketPtr->getAny());
		}
		return result;
	}

	EqualityBucket* getBucket(T val) const {
		auto it = mapToBucket.find(val);
		return it != mapToBucket.end() ? it->second : nullptr;
	}

public:

	ValueRelations() = default;
	
	ValueRelations(const ValueRelations& other):
		lastPlaceholderId(other.lastPlaceholderId) {

		std::map<EqualityBucket*, EqualityBucket*> oldToNewPtr;

		// create new copies of buckets
		for(const std::unique_ptr<EqualityBucket>& bucketUniquePtr : other.buckets) {
			assert(bucketUniquePtr);
			assert(bucketUniquePtr.get());
			
			EqualityBucket* newBucketPtr = new EqualityBucket(*bucketUniquePtr);
			buckets.emplace_back(newBucketPtr);

			oldToNewPtr.emplace(bucketUniquePtr.get(), newBucketPtr);
		}

		// set successors to point to new copies
		for (const std::unique_ptr<EqualityBucket>& bucketUniquePtr : buckets)
			bucketUniquePtr->substitueAll(oldToNewPtr);

		// set map to use new copies
		for (auto& pair : other.mapToBucket)
			mapToBucket.emplace(pair.first, oldToNewPtr[pair.second]);

		// set placeholder buckets to use new copies
		for (auto& pair : other.placeholderBuckets)
			placeholderBuckets.emplace(pair.first, oldToNewPtr[pair.second]);

		// set nonEqualities to use new copies
		for (auto& pair : other.nonEqualities) {
			auto returnPair = nonEqualities.emplace(oldToNewPtr[pair.first], pair.second);
			substitueInSet(oldToNewPtr, returnPair.first->second);
		}

		// set loads to use new copies
		for (auto& pair : other.loads)
			loads.emplace(oldToNewPtr[pair.first], oldToNewPtr[pair.second]);
		
	}

	friend void swap(ValueRelations& first, ValueRelations& second) {
		using std::swap;

		swap(first.buckets, second.buckets);
		swap(first.mapToBucket, second.mapToBucket);
		swap(first.placeholderBuckets, second.placeholderBuckets);
		swap(first.lastPlaceholderId, second.lastPlaceholderId);
		swap(first.nonEqualities, second.nonEqualities);
		swap(first.loads, second.loads);
	}

	ValueRelations& operator=(ValueRelations other) {
		swap(*this, other);

		return *this;
	}

	bool hasAllRelationsFrom(const ValueRelations& other) const {
		return getExtraRelationsIn(other).empty();
	}

	bool merge(const ValueRelations& other, bool relationsOnly = false) {
		ValueRelations original = *this;

		std::vector<std::tuple<EqualityBucket*, EqualityBucket*, Relation>> missingRelations;
		missingRelations = getExtraRelationsIn(other);

		EqualityBucket* otherBucket;
		EqualityBucket* otherRelatedBucket;
		Relation relation;
		for (auto& tuple : missingRelations) {
			std::tie(otherRelatedBucket, otherBucket, relation) = tuple;

			EqualityBucket* thisBucket = getCorrespondingBucketOrNew(other, otherBucket);
			EqualityBucket* thisRelatedBucket = getCorrespondingBucketOrNew(other, otherRelatedBucket);
			assert(thisBucket && thisRelatedBucket);

			if (hasConflictingRelation(thisRelatedBucket, thisBucket, relation)) {
				swap(*this, original);
				return false;
			}

			switch (relation) {
				case Relation::EQ:
					for (T val : otherRelatedBucket->getEqual()) {
						if (hasConflictingRelation(val, otherRelatedBucket->getEqual()[0], Relation::EQ)) {
							swap(*this, original);
							return false;
						}
						add(val);
						setEqual(thisRelatedBucket, mapToBucket[val]);
						thisRelatedBucket = getCorrespondingBucketOrNew(other, otherRelatedBucket);
					}
					break;
				case Relation::NE: setNonEqual(thisRelatedBucket, thisBucket);
								   break;
				case Relation::LT: setLesser(thisRelatedBucket, thisBucket);
								   break;
				case Relation::LE: setLesserEqual(thisRelatedBucket, thisBucket);
								   break;
				case Relation::LOAD: if (!relationsOnly)
										 setLoad(thisBucket, thisRelatedBucket);
									 break;
				default: assert(0 && "GE and GT cannot occurr");
			}
		}

		return true;
	}

	EqualityBucket* add(T val) {
		if (EqualityBucket* bucket = getBucket(val))
			return bucket;

		auto constVal = llvm::dyn_cast<llvm::ConstantInt>(val);
		if (constVal) {
			for (auto& bucketUniquePtr : buckets) {
				EqualityBucket* otherBucket = bucketUniquePtr.get();
				C constBucket = getEqualConstant(otherBucket);
				if (!constBucket)
					continue;

				int64_t newInt = constVal->getSExtValue();
				int64_t oldInt = constBucket->getSExtValue();

				if (newInt == oldInt) {
					mapToBucket.emplace(val, otherBucket);
					otherBucket->getEqual().emplace_back(val);
					return otherBucket;
				}
			}
		}

		// else added value will surely be in a bucket of its own
		EqualityBucket* newBucketPtr = new EqualityBucket;
		buckets.emplace_back(newBucketPtr);
		mapToBucket.emplace(val, newBucketPtr);
		newBucketPtr->getEqual().emplace_back(val);

		if (!constVal)
			return newBucketPtr;
		// else add all relations to other constants

		for (auto& bucketUniquePtr : buckets) {
			EqualityBucket* otherBucket = bucketUniquePtr.get();
			C constBucket = getEqualConstant(otherBucket);
			if (!constBucket)
				continue;

			int64_t newInt = constVal->getSExtValue();
			int64_t oldInt = constBucket->getSExtValue();

			if (newInt < oldInt) setLesser(newBucketPtr, otherBucket);
			if (newInt > oldInt) setLesser(otherBucket, newBucketPtr);
		}

		return newBucketPtr;
	}

	// DANGER setEqual invalidates all EqualityBucket*
	void setEqual(T lt, T rt) {
		EqualityBucket* ltBucket = add(lt);
		EqualityBucket* rtBucket = add(rt);
		setEqual(ltBucket, rtBucket);
	}

	void setEqual(T lt, unsigned rt) {
		EqualityBucket* ltBucket = add(lt);
		setEqual(ltBucket, placeholderBuckets[rt]);
	}

	void setEqual(unsigned lt, T rt) {
		setEqual(rt, lt);
	}

	void setNonEqual(T lt, T rt) {
		EqualityBucket* ltBucket = add(lt);
		EqualityBucket* rtBucket = add(rt);
		setNonEqual(ltBucket, rtBucket);
	}

	void setNonEqual(T lt, unsigned rt) {
		EqualityBucket* ltBucket = add(lt);
		setNonEqual(ltBucket, placeholderBuckets[rt]);
	}

	void setNonEqual(unsigned lt, T rt) {
		EqualityBucket* rtBucket = add(rt);
		setNonEqual(placeholderBuckets[lt], rtBucket);
	}

	void setLesser(T lt, T rt) {
		EqualityBucket* ltBucket = add(lt);
		EqualityBucket* rtBucket = add(rt);
		setLesser(ltBucket, rtBucket);
	}

	void setLesser(T lt, unsigned rt) {
		EqualityBucket* ltBucket = add(lt);
		setLesser(ltBucket, placeholderBuckets[rt]);
	}

	void setLesser(unsigned lt, T rt) {
		EqualityBucket* rtBucket = add(rt);
		setLesser(placeholderBuckets[lt], rtBucket);
	}

	void setLesserEqual(T lt, T rt) {
		EqualityBucket* ltBucket = add(lt);
		EqualityBucket* rtBucket = add(rt);
		setLesserEqual(ltBucket, rtBucket);
	}

	void setLesserEqual(T lt, unsigned rt) {
		EqualityBucket* ltBucket = add(lt);
		setLesserEqual(ltBucket, placeholderBuckets[rt]);
	}

	void setLesserEqual(unsigned lt, T rt) {
		EqualityBucket* rtBucket = add(rt);
		setLesserEqual(placeholderBuckets[lt], rtBucket);
	}

	void setLoad(T from, T val) {
		EqualityBucket* valBucket = add(val);
		EqualityBucket* fromBucket = add(from);
		setLoad(fromBucket, valBucket);
	}

	void setLoad(T from, unsigned val) {
		EqualityBucket* fromBucket = add(from);
		setLoad(fromBucket, placeholderBuckets[val]);
	}

	void unsetAllLoadsByPtr(T from) {
		EqualityBucket* fromBucketPtr = getBucket(from);
		if (!inGraph(fromBucketPtr))
			return;

		EqualityBucket* valBucketPtr = findByKey(loads, fromBucketPtr);
		if (!inGraph(valBucketPtr))
			return; // from doesn't load anything

		loads.erase(fromBucketPtr);

		for (auto& pair : placeholderBuckets) {
			if (pair.second == valBucketPtr) {
				unsetComparativeRelations(valBucketPtr);
				placeholderBuckets.erase(pair.first);
				break;
			}
		}

		if (!hasComparativeRelationsOrLoads(valBucketPtr)) {
			if (!valBucketPtr->getEqual().empty()) {
				T val = valBucketPtr->getAny();
				mapToBucket.erase(val);
			}
			eraseUniquePtr(buckets, valBucketPtr);
		}
		if (!hasComparativeRelationsOrLoads(fromBucketPtr)) {
			mapToBucket.erase(from);
			eraseUniquePtr(buckets, fromBucketPtr);
		}
	}

	void unsetAllLoads() {
        loads.clear();
		
		for (auto it = buckets.begin(); it != buckets.end(); ) {
			if (!hasComparativeRelations(it->get())) {
				if (!(*it)->getEqual().empty())
					mapToBucket.erase((*it)->getAny());

				it = buckets.erase(it);
			} else
				++it;
		}
    }

	void unsetComparativeRelations(T val) {
		EqualityBucket* valBucketPtr = getBucket(val);
		if (!inGraph(valBucketPtr))
			return;

		bool onlyReference = valBucketPtr->getEqual().size() == 1;
		if (!onlyReference) {
			// val moves to its own equality bucket
			mapToBucket.erase(val);
			add(val);
		} else
			unsetComparativeRelations(valBucketPtr);
	}

	bool isEqual(T lt, T rt) const {

		C constLt = llvm::dyn_cast<llvm::ConstantInt>(lt);
		C constRt = llvm::dyn_cast<llvm::ConstantInt>(rt);

		EqualityBucket* ltBucketPtr = getBucket(lt);
		EqualityBucket* rtBucketPtr = getBucket(rt);

		if (!inGraph(ltBucketPtr) && !inGraph(rtBucketPtr))
			return constLt && constRt
				&& constLt->getSExtValue() == constRt->getSExtValue();

		if (!inGraph(ltBucketPtr)) {
			std::swap(lt, rt);
			std::swap(constLt, constRt);
			std::swap(ltBucketPtr, rtBucketPtr);
		}
		
		if (!inGraph(rtBucketPtr)) {
			assert(inGraph(ltBucketPtr));
			C ltEqual = getEqualConstant(ltBucketPtr);
			if (!constRt || !ltEqual)
				return false;
			return constRt->getSExtValue() == ltEqual->getSExtValue();
		}

		assert(inGraph(ltBucketPtr) && inGraph(rtBucketPtr));
		return isEqual(ltBucketPtr, rtBucketPtr);
	}

	bool isNonEqual(T lt, T rt) const {

		C constLt = llvm::dyn_cast<llvm::ConstantInt>(lt);
		C constRt = llvm::dyn_cast<llvm::ConstantInt>(rt);

		EqualityBucket* ltBucketPtr = getBucket(lt);
		EqualityBucket* rtBucketPtr = getBucket(rt);

		if (!inGraph(ltBucketPtr) && !inGraph(rtBucketPtr))
			return constLt && constRt
				&& constLt->getSExtValue() != constRt->getSExtValue();

		if (!inGraph(ltBucketPtr)) {
			std::swap(lt, rt);
			std::swap(constLt, constRt);
			std::swap(ltBucketPtr, rtBucketPtr);
		}

		if (!inGraph(rtBucketPtr)) {
			assert (inGraph(ltBucketPtr));
			C ltEqual = getEqualConstant(ltBucketPtr);
			if (!constRt || !ltEqual)
				return false;
			return constRt->getSExtValue() != ltEqual->getSExtValue();
		}

		assert(inGraph(ltBucketPtr) && inGraph(rtBucketPtr));
		return isNonEqual(ltBucketPtr, rtBucketPtr);
	}

	bool isLesser(T lt, T rt) const {

		C constLt = llvm::dyn_cast<llvm::ConstantInt>(lt);
		C constRt = llvm::dyn_cast<llvm::ConstantInt>(rt);

		EqualityBucket* ltBucketPtr = getBucket(lt);
		EqualityBucket* rtBucketPtr = getBucket(rt);

		if (!inGraph(ltBucketPtr) && !inGraph(rtBucketPtr))
			return constLt && constRt
				&& constLt->getSExtValue() < constRt->getSExtValue();

		if (!inGraph(rtBucketPtr)) {
			C constBound = getUpperBound(ltBucketPtr, true);
			if (constBound && constRt && constBound->getSExtValue() <= constRt->getSExtValue())
				return true;
			constBound = getUpperBound(ltBucketPtr, false);
			return constBound && constRt && constBound->getSExtValue() < constRt->getSExtValue();
		}

		if (!inGraph(ltBucketPtr)) {
			C constBound = getLowerBound(rtBucketPtr, true);
			if (constLt && constBound && constLt->getSExtValue() <= constBound->getSExtValue())
				return true;
			constBound = getLowerBound(rtBucketPtr, false);
			return constLt && constBound && constLt->getSExtValue() < constBound->getSExtValue();
		}

		assert (inGraph(ltBucketPtr) && inGraph(rtBucketPtr));
		return isLesser(ltBucketPtr, rtBucketPtr);
	}

	bool isLesserEqual(T lt, T rt) const {

		C constLt = llvm::dyn_cast<llvm::ConstantInt>(lt);
		C constRt = llvm::dyn_cast<llvm::ConstantInt>(rt);

		EqualityBucket* ltBucketPtr = getBucket(lt);
		EqualityBucket* rtBucketPtr = getBucket(rt);

		if (!inGraph(ltBucketPtr) && !inGraph(rtBucketPtr))
			return constLt && constRt
				&& constLt->getSExtValue() <= constRt->getSExtValue();

		if (!inGraph(rtBucketPtr)) {
			C constBound = getUpperBound(ltBucketPtr, false);
			return constBound && constRt && constBound->getSExtValue() <= constRt->getSExtValue();
		}

		if (!inGraph(ltBucketPtr)) {
			C constBound = getLowerBound(rtBucketPtr, false);
			return constLt && constBound && constLt->getSExtValue() <= constBound->getSExtValue();
		}

		assert (inGraph(ltBucketPtr) && inGraph(rtBucketPtr));
		return isLesserEqual(ltBucketPtr, rtBucketPtr);
	}

	bool hasConflictingRelation(T lt, T rt, Relation relation) const {
		switch (relation) {
			case Relation::EQ:
				return isNonEqual(lt, rt)
					|| isLesser(lt, rt)
					|| isLesser(rt, lt);

			case Relation::NE:
				return isEqual(lt, rt);

			case Relation::LT:
				return isLesserEqual(rt, lt);

			case Relation::LE:
				return isLesser(rt, lt);

			case Relation::GT:
				return hasConflictingRelation(rt, lt, Relation::LT);

			case Relation::GE:
				return hasConflictingRelation(rt, lt, Relation::LE);

			case Relation::LOAD:
				return hasLoad(rt) && inGraph(lt)
					&& hasConflictingRelation(mapToBucket.at(lt), loads.at(mapToBucket.at(rt)), Relation::EQ);
		}
		assert(0 && "unreachable");
		abort();
	}

	bool isLoad(T from, T val) const {

		EqualityBucket* fromBucketPtr = getBucket(from);
		EqualityBucket* valBucketPtr = getBucket(val);
		
		if (!inGraph(fromBucketPtr) || !inGraph(valBucketPtr))
			return false;
	
		return isLoad(fromBucketPtr, valBucketPtr);
	}

	bool hasLoad(T from) const {

		EqualityBucket* fromBucketPtr = getBucket(from);

		if (!inGraph(fromBucketPtr))
			return false;

		return hasLoad(fromBucketPtr);
	}

	std::vector<T> getEqual(T val) const {
		std::vector<T> result;
		if (mapToBucket.find(val) == mapToBucket.end()) {
			result.push_back(val);
			return result;
		}
		
		const EqualityBucket* valBucket = mapToBucket.at(val);
		return valBucket->getEqual();
	}

	ValueIterator begin_lesser(T val) const {
		return ValueIterator(mapToBucket.at(val), true, ValueIterator::Type::DOWN, true);
	}

	ValueIterator end_lesser(T val) const {
		return ValueIterator(mapToBucket.at(val), true, ValueIterator::Type::DOWN, false);
	}

	ValueIterator begin_lesserEqual(T val) const {
		return ValueIterator(mapToBucket.at(val), false, ValueIterator::Type::DOWN, true);
	}

	ValueIterator end_lesserEqual(T val) const {
		return ValueIterator(mapToBucket.at(val), false, ValueIterator::Type::DOWN, false);
	}

	ValueIterator begin_greater(T val) const {
		return ValueIterator(mapToBucket.at(val), true, ValueIterator::Type::UP, true);
	}

	ValueIterator end_greater(T val) const {
		return ValueIterator(mapToBucket.at(val), true, ValueIterator::Type::UP, false);
	}

	ValueIterator begin_greaterEqual(T val) const {
		return ValueIterator(mapToBucket.at(val), false, ValueIterator::Type::UP, true);
	}

	ValueIterator end_greaterEqual(T val) const {
		return ValueIterator(mapToBucket.at(val), false, ValueIterator::Type::UP, false);
	}

	ValueIterator begin_all(T val) const {
		// TODO add non-equal values
		return ValueIterator(mapToBucket.at(val), false, ValueIterator::Type::ALL, true);
	}

	ValueIterator end_all(T val) const {
		return ValueIterator(mapToBucket.at(val), false, ValueIterator::Type::ALL, false);
	}

	std::vector<T> getDirectlyLesser(T val) const {
		return getDirectlyRelated(val, true);
	}

	std::vector<T> getDirectlyGreater(T val) const {
		return getDirectlyRelated(val, false);
	}

	std::vector<T> getAllRelated(T val) const {
		std::vector<T> result;
		for (auto it = begin_all(val); it != end_all(val); ++it) {
			result.push_back((*it).first);
		}
		return result;
	}

	C getLesserEqualBound(T val) const {

		if (!inGraph(val))
			return llvm::dyn_cast<llvm::ConstantInt>(val);
		return getLowerBound(mapToBucket.at(val), false);
	}

	C getGreaterEqualBound(T val) const {

		if (!inGraph(val))
			return llvm::dyn_cast<llvm::ConstantInt>(val);
		return getUpperBound(mapToBucket.at(val), false);
	}

	std::vector<T> getAllValues() const {
		std::vector<T> result;
		for (auto& pair : mapToBucket)
			result.push_back(pair.first);
		return result;
	}

	std::vector<T> getPtrsByVal(T val) {
		if (!inGraph(val))
			return std::vector<T>();
		EqualityBucket* valBucketPtr = mapToBucket.at(val);

		EqualityBucket* fromBucketPtr = findByValue(loads, valBucketPtr);
		return fromBucketPtr ? fromBucketPtr->getEqual() : std::vector<T>();
	}

	const std::vector<T> getValsByPtr(T from) const {
		if (!inGraph(from))
			return std::vector<T>();
		EqualityBucket* fromBucketPtr = mapToBucket.at(from);

		EqualityBucket* valBucketPtr = findByKey(loads, fromBucketPtr);
		return valBucketPtr ? valBucketPtr->getEqual() : std::vector<T>();
	}

	std::set<std::pair<std::vector<T>, std::vector<T>>> getAllLoads() const {
		std::set<std::pair<std::vector<T>, std::vector<T>>> result;
		for (const auto& pair : loads) {
			result.emplace(pair.first->getEqual(), pair.second->getEqual());
		}
		return result;
	}

	const std::vector<bool>& getValidAreas() const {
		return validAreas;
	}

	std::vector<bool>& getValidAreas() {
		return validAreas;
	}

	bool hasComparativeRelations(unsigned placeholder) const {
		if (placeholderBuckets.find(placeholder) == placeholderBuckets.end())
			return false;
		
		return hasComparativeRelations(placeholderBuckets.at(placeholder));
	}

	unsigned newPlaceholderBucket() {
		EqualityBucket* bucket = new EqualityBucket;
		buckets.emplace_back(bucket);
		placeholderBuckets.emplace(++lastPlaceholderId, bucket);
		return lastPlaceholderId;
	}

	void erasePlaceholderBucket(unsigned id) {
		// DANGER erases bucket for good, not just
		// the mention in placeholderBuckets
		EqualityBucket* bucket = placeholderBuckets[id];
		
		eraseUniquePtr(buckets, bucket);
		placeholderBuckets.erase(id);
	}

	bool holdsAnyRelations() const {
		return !buckets.empty();
	}

#ifndef NDEBUG
	std::string strip(std::string str) const {
		std::string result;
		int space_counter = 0;
		for (char c : str) {
			if (c != ' ' || ++space_counter <= 2) {
				result += c;
			} else
				break;
		}
		return result;
	}

	void printVals(std::ostream& stream, const EqualityBucket* bucket) const {
		stream << "{ ";
		stream.flush();

		for (auto pair : placeholderBuckets) {
			if (pair.second == bucket) stream << "placeholder " << pair.first << " ";
		}

		for (auto val : bucket->getEqual()) {
			stream << strip(debug::getValName(val)) << "; ";
		}

		stream << "}";
	}

	void printInterleaved(std::ostream& stream, const EqualityBucket* e1,
						  std::string sep, const EqualityBucket* e2) const {
		printVals(stream, e1);
		stream << sep;
		printVals(stream, e2);
		if (&stream == &std::cout)
			stream << "\\r";
		else
			stream << std::endl;
	}

	void dump() {
		generalDump(std::cout);
	}

	void ddump() {
		generalDump(std::cerr);
	}

	void ddump(EqualityBucket* bucket, bool just = false) {
		if (just)
			printVals(std::cerr, bucket);
		else
			dump(std::cerr, bucket);
	}

	void ddump(const llvm::Value* val) {
		if (!inGraph(val))
			return;

		std::cerr << debug::getValName(val) << ":" << std::endl;
		dump(std::cerr, mapToBucket.at(val));
		std::cerr << std::endl;
	}

	void dump(std::ostream& stream, EqualityBucket* bucket) {
		for (auto ptr : bucket->lesser)
			printInterleaved(stream, ptr, " < ", bucket);

		for (auto ptr : bucket->lesserEqual)
			printInterleaved(stream, ptr, " <= ", bucket);

		auto foundNonEqual = nonEqualities.find(bucket);
		if (foundNonEqual != nonEqualities.end()) {
			for (EqualityBucket* nonEqual : foundNonEqual->second)
				if (nonEqual < bucket)
					printInterleaved(stream, nonEqual, " != ", bucket);
		}

		EqualityBucket* foundValue = findByKey(loads, bucket);
		if (foundValue)
			printInterleaved(stream, foundValue, " = LOAD ", bucket);

		if (bucket->lesser.empty() // values just equal and nothing else
				&& bucket->lesserEqual.empty()
				&& bucket->parents.empty()
				&& foundNonEqual == nonEqualities.end()
				&& !findByValue(loads, bucket)
				&& !foundValue) {
			printVals(stream, bucket);
			stream << std::endl;
		}
	}

    void generalDump(std::ostream& stream) {

		for (const auto& bucketPtr : buckets) {
			dump(stream, bucketPtr.get());
		}

    }
#endif

};

} // namespace vr
} // namespace dg

#endif // DG_LLVM_RELATIONS_MAP_H_
