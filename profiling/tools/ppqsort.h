#pragma once
#include <algorithm>
namespace ppqsort {
namespace execution { inline constexpr int par = 0; }
template<class I, class... C> void sort(int, I a, I b, C... c) { std::sort(a,b,c...); }
template<class I, class... C> void sort(I a, I b, C... c) { std::sort(a,b,c...); }
}
