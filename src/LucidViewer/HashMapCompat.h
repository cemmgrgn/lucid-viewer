#pragma once

// stdext::hash_map was removed from newer Visual C++ versions (MSVC 14.5x), this maps it to std::unordered_map.
// A hash compare traits class as used by stdext::hash_map (hash operator and 'less' operator) is adapted to the
// hash and equality functors needed by std::unordered_map.

#include <unordered_map>
#include <functional>

template<class TTraits> struct CHashTraitsHasher {
	template<class TKey> size_t operator()(const TKey& key) const { return TTraits()(key); }
};

template<class TTraits> struct CHashTraitsEqual {
	template<class TKey> bool operator()(const TKey& key1, const TKey& key2) const {
		TTraits traits;
		return !traits(key1, key2) && !traits(key2, key1);
	}
};

template<class TKey, class TTraits> struct CHashMapPolicy {
	typedef CHashTraitsHasher<TTraits> Hasher;
	typedef CHashTraitsEqual<TTraits> Equal;
};

template<class TKey> struct CHashMapPolicy<TKey, void> {
	typedef std::hash<TKey> Hasher;
	typedef std::equal_to<TKey> Equal;
};

namespace stdext {
	template<class TKey, class TValue, class TTraits = void>
	using hash_map = std::unordered_map<TKey, TValue, typename CHashMapPolicy<TKey, TTraits>::Hasher, typename CHashMapPolicy<TKey, TTraits>::Equal>;
}
