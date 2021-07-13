#pragma once

#include <chopper/hierarchical_interleaved_bloom_filter.hpp>
#include <chopper/search/pair_hash.hpp>
#include <chopper/search/search_config.hpp>
#include <chopper/search/sync_out.hpp>

inline void clear_and_compute_kmers(std::vector<size_t> & kmers, seqan3::dna4_vector const & query, search_config const & config)
{
    kmers.clear();
    auto hash_view = query | seqan3::views::kmer_hash(seqan3::ungapped{config.k});
    std::ranges::move(hash_view, std::cpp20::back_inserter(kmers));
}

template <seqan3::data_layout data_layout_mode>
inline void write_header(hibf::hierarchical_interleaved_bloom_filter<data_layout_mode> const & hibf, sync_out & out_stream)
{
    hibf.user_bins.write_filenames(out_stream);
    out_stream << "#QUERY_NAME\tUSER_BINS\n";
}

inline void write_result(std::string & line,
                         std::vector<int64_t> const & result,
                         std::string const & id,
                         sync_out & out_stream)
{
    line.clear();
    line += id;
    line += '\t';

    if (result.empty())
    {
        line += '\n';
        out_stream << line;
        return;
    }

    for (auto && idx : result)
    {
        line += std::to_string(idx);
        line += ',';
    }

    line.back() = '\n';
    out_stream << line;
}

template <seqan3::data_layout data_layout_mode>
inline void search(std::vector<std::pair<int32_t, uint32_t>> & membership_result,
                   std::vector<size_t> const & kmers,
                   hibf::hierarchical_interleaved_bloom_filter<data_layout_mode> const & hibf,
                   search_config const & config,
                   int64_t const ibf_idx)
{
    size_t const kmer_lemma = (kmers.size() > (config.errors + 1) * config.k)
                              ? kmers.size() - (config.errors + 1) * config.k
                              : 0;

    auto counting_agent = hibf.ibf_vector[ibf_idx].template counting_agent<uint16_t>();
    auto const & result = counting_agent.bulk_count(kmers);
    assert(result.size() > 0);

    size_t sum{};

    for (size_t bin{}; bin < result.size(); ++bin)
    {
        sum += result[bin];
        auto const current_filename_index = hibf.user_bins.filename_index(ibf_idx, bin);

        if (current_filename_index < 0) // merged bin
        {
            // if threshold, next level
            if (sum >= kmer_lemma)
                search(membership_result, kmers, hibf, config, hibf.next_ibf_id[ibf_idx][bin]);
            sum = 0;
        }
        else if (bin == result.size() - 1 || // last bin
                 current_filename_index != hibf.user_bins.filename_index(ibf_idx, bin + 1)) // end of split bin
        {
            // if threshold, write
            if (sum >= kmer_lemma)
                membership_result.emplace_back(ibf_idx, bin);
            sum = 0;
        }
    }
}
