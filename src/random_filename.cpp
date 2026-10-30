#include "mesher/random_filename.hpp"
#include "mesher_embedded_filename_words.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace mesher {
namespace {
bool portableWord(std::string_view word) {
    if(word.empty()||word.size()>48)return false;
    for(const unsigned char c:word)
        if(!((c>='a'&&c<='z')||c=='-'))return false;
    return word.front()!='-'&&word.back()!='-';
}

std::vector<std::string_view> dictionary(std::string_view text) {
    std::vector<std::string_view> words;
    std::unordered_set<std::string_view> found;
    std::size_t pos=0;
    while(pos<text.size()) {
        auto end=text.find('\n',pos);
        if(end==std::string_view::npos)end=text.size();
        auto word=text.substr(pos,end-pos);
        if(!word.empty()&&word.back()=='\r')word.remove_suffix(1);
        if(!portableWord(word)||!found.insert(word).second)
            throw std::runtime_error(
                "Embedded random filename word list has an invalid/duplicate word");
        words.push_back(word);
        pos=end+1;
    }
    if(words.empty())
        throw std::runtime_error("Embedded random filename word list is empty");
    return words;
}

const std::vector<std::string_view>& adjectives() {
    static const auto words=dictionary(embedded_filename_words::adjectives);
    return words;
}
const std::vector<std::string_view>& nouns() {
    static const auto words=dictionary(embedded_filename_words::nouns);
    return words;
}
} // namespace

std::pair<std::size_t,std::size_t> embeddedFilenameWordCounts() {
    return {adjectives().size(),nouns().size()};
}

std::string randomFilenameStem(std::mt19937_64& random) {
    const auto& a=adjectives();
    const auto& n=nouns();
    const auto first=std::uniform_int_distribution<std::size_t>(
        0,a.size()-1)(random);
    const auto second=std::uniform_int_distribution<std::size_t>(
        0,n.size()-1)(random);
    return std::string(a[first])+" "+std::string(n[second]);
}

std::string randomFilenameStem() {
    // Seed with more than one 32-bit entropy sample; the engine is private
    // per thread and never perturbs the sculpt/repair geometry RNG.
    thread_local std::mt19937_64 random([] {
        std::random_device source;
        std::array<std::uint32_t,8> seedValues{};
        for(auto& value:seedValues)value=source();
        std::seed_seq seed(seedValues.begin(),seedValues.end());
        return std::mt19937_64(seed);
    }());
    return randomFilenameStem(random);
}
} // namespace mesher
