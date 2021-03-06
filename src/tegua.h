/*
============================================================================
DELLY: Structural variant discovery by integrated PE mapping and SR analysis
============================================================================
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
============================================================================
Contact: Tobias Rausch (rausch@embl.de)
============================================================================
*/

#ifndef TEGUA_H
#define TEGUA_H

#include <boost/unordered_map.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/filesystem.hpp>
#include <boost/tokenizer.hpp>
#include <iostream>
#include <vector>
#include <htslib/vcf.h>
#include <htslib/sam.h>
#include <math.h>
#include <stdio.h>

#include <htslib/sam.h>
#include <htslib/vcf.h>
#include <htslib/faidx.h>

#include "delly.h"
#include "coverage.h"
#include "util.h"
#include "junction.h"
#include "cluster.h"
#include "assemble.h"
#include "modvcf.h"

namespace torali {


  struct TeguaConfig {
    bool hasDumpFile;
    bool hasVcfFile;
    bool isHaplotagged;
    uint16_t minMapQual;
    uint16_t minGenoQual;
    uint32_t minClip;
    uint32_t minRefSep;
    uint32_t maxReadSep;
    uint32_t graphPruning;
    int32_t nchr;
    int32_t minimumFlankSize;
    int32_t indelsize;
    float indelExtension;
    float flankQuality;
    std::vector<int32_t> chrlen;
    std::string svtype;
    DnaScore<int> aliscore;
    boost::filesystem::path dumpfile;
    boost::filesystem::path outfile;
    boost::filesystem::path vcffile;
    std::vector<boost::filesystem::path> files;
    boost::filesystem::path genome;
  };
  

 template<typename TConfig>
 inline int32_t
 runTegua(TConfig& c) {
   typedef std::vector<SRBamRecord> TSRBamRecord;
   typedef std::vector<TSRBamRecord> TSvtSRBamRecord;

   // Dummy library objects
   typedef std::vector<LibraryInfo> TSampleLibrary;
   TSampleLibrary sampleLib;
   
   // Structural Variants
   typedef std::vector<StructuralVariantRecord> TVariants;
   TVariants svs;

   // Open header
   samFile* samfile = sam_open(c.files[0].string().c_str(), "r");
   bam_hdr_t* hdr = sam_hdr_read(samfile);

   // SV Discovery
   if (!c.hasVcfFile) {
     // SR Store
     typedef std::vector<SeqSlice> TSvPosVector;
     typedef boost::unordered_map<std::size_t, TSvPosVector> TReadSV;
     TReadSV srStore;

     // Find junctions
     {
       TSvtSRBamRecord srBR(2 * DELLY_SVT_TRANS, TSRBamRecord());
       findJunctions(c, srBR);
   
       // Debug
       if (c.hasDumpFile) outputSRBamRecords(c, srBR);

       // Cluster BAM records
       for(uint32_t svt = 0; svt < srBR.size(); ++svt) {
	 if (srBR[svt].empty()) continue;
	 
	 // Sort
	 std::sort(srBR[svt].begin(), srBR[svt].end(), SortSRBamRecord<SRBamRecord>());
	 
	 // Cluster
	 cluster(c, srBR[svt], svs, c.maxReadSep, svt);
	 
	 // Track split-reads
	 for(uint32_t i = 0; i < srBR[svt].size(); ++i) {
	   // Read assigned?
	   if (srBR[svt][i].svid != -1) {
	     if (srStore.find(srBR[svt][i].id) == srStore.end()) srStore.insert(std::make_pair(srBR[svt][i].id, TSvPosVector()));
	     srStore[srBR[svt][i].id].push_back(SeqSlice(srBR[svt][i].svid, srBR[svt][i].sstart, srBR[svt][i].inslen));
	   }
	 }
       }
     }
     
     // Assemble
     assemble(c, srStore, svs);
   } else vcfParse(c, hdr, svs);

   // ReSort SVs
   std::sort(svs.begin(), svs.end(), SortSVs<StructuralVariantRecord>());
   uint32_t cliqueCount = 0;
   for(typename TVariants::iterator svIt = svs.begin(); svIt != svs.end(); ++svIt, ++cliqueCount) svIt->id = cliqueCount;

   // Annotate junction reads
   typedef std::vector<JunctionCount> TSVJunctionMap;
   typedef std::vector<TSVJunctionMap> TSampleSVJunctionMap;
   TSampleSVJunctionMap junctionCountMap;
   
   // Annotate spanning coverage
   typedef std::vector<SpanningCount> TSVSpanningMap;
   typedef std::vector<TSVSpanningMap> TSampleSVSpanningMap;
   TSampleSVSpanningMap spanCountMap;
   
   // Annotate coverage
   typedef std::vector<ReadCount> TSVReadCount;
   typedef std::vector<TSVReadCount> TSampleSVReadCount;
   TSampleSVReadCount rcMap;
   
   // SV Genotyping
   //if (!svs.empty()) _annotateCoverage(c, hdr, sampleLib, svs, rcMap, junctionCountMap, spanCountMap);
   
   // VCF Output
   outputVcf(c, svs);

   // Clean-up
   bam_hdr_destroy(hdr);
   sam_close(samfile);
   
   return 0;
 }

 int teguaMain(int argc, char **argv) {
   TeguaConfig c;
   c.isHaplotagged = false;
   
   // Parameter
   boost::program_options::options_description generic("Generic options");
   generic.add_options()
     ("help,?", "show help message")
     ("svtype,t", boost::program_options::value<std::string>(&c.svtype)->default_value("ALL"), "SV type to compute [DEL, INS, DUP, INV, BND, ALL]")
     ("genome,g", boost::program_options::value<boost::filesystem::path>(&c.genome), "genome fasta file")
     ("outfile,o", boost::program_options::value<boost::filesystem::path>(&c.outfile)->default_value("sv.bcf"), "SV BCF output file")
     ;
   
   boost::program_options::options_description disc("Discovery options");
   disc.add_options()
     ("extension,e", boost::program_options::value<float>(&c.indelExtension)->default_value(0.6), "enforce indel extension")
     ("mapqual,q", boost::program_options::value<uint16_t>(&c.minMapQual)->default_value(10), "min. mapping quality")
     ("minclip,c", boost::program_options::value<uint32_t>(&c.minClip)->default_value(50), "min. clipping length")
     ("minrefsep,m", boost::program_options::value<uint32_t>(&c.minRefSep)->default_value(50), "min. reference separation")
     ("maxreadsep,n", boost::program_options::value<uint32_t>(&c.maxReadSep)->default_value(75), "max. read separation")
     ;

   boost::program_options::options_description geno("Genotyping options");
   geno.add_options()
     ("vcffile,v", boost::program_options::value<boost::filesystem::path>(&c.vcffile), "input VCF/BCF file for genotyping")
     ("geno-qual,u", boost::program_options::value<uint16_t>(&c.minGenoQual)->default_value(5), "min. mapping quality for genotyping")
     ("dump,d", boost::program_options::value<boost::filesystem::path>(&c.dumpfile), "gzipped output file for SV-reads (optional)")
     ;

   
   
   boost::program_options::options_description hidden("Hidden options");
   hidden.add_options()
     ("input-file", boost::program_options::value< std::vector<boost::filesystem::path> >(&c.files), "input file")
     ("pruning,j", boost::program_options::value<uint32_t>(&c.graphPruning)->default_value(1000), "graph pruning cutoff")
     ;
   
   boost::program_options::positional_options_description pos_args;
   pos_args.add("input-file", -1);
   
   boost::program_options::options_description cmdline_options;
   cmdline_options.add(generic).add(disc).add(geno).add(hidden);
   boost::program_options::options_description visible_options;
   visible_options.add(generic).add(disc).add(geno);
   boost::program_options::variables_map vm;
   boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(cmdline_options).positional(pos_args).run(), vm);
   boost::program_options::notify(vm);
   
   // Check command line arguments
   if ((vm.count("help")) || (!vm.count("input-file")) || (!vm.count("genome"))) {
     std::cout << "Usage: dellyLR " << argv[0] << " [OPTIONS] -g <ref.fa> <input.bam>" << std::endl;
     std::cout << visible_options << "\n";
    return 1;
   }

   // Dump reads
   if (vm.count("dump")) c.hasDumpFile = true;
   else c.hasDumpFile = false;
   
   // Check input BAM
   {
     if (!(boost::filesystem::exists(c.files[0]) && boost::filesystem::is_regular_file(c.files[0]) && boost::filesystem::file_size(c.files[0]))) {
       std::cerr << "Alignment file is missing: " << c.files[0].string() << std::endl;
       return 1;
     }
     samFile* samfile = sam_open(c.files[0].string().c_str(), "r");
     if (samfile == NULL) {
       std::cerr << "Fail to open file " << c.files[0].string() << std::endl;
       return 1;
     }
     hts_idx_t* idx = sam_index_load(samfile, c.files[0].string().c_str());
     if (idx == NULL) {
       std::cerr << "Fail to open index for " << c.files[0].string() << std::endl;
       return 1;
     }
     bam_hdr_t* hdr = sam_hdr_read(samfile);
     if (hdr == NULL) {
       std::cerr << "Fail to open header for " << c.files[0].string() << std::endl;
       return 1;
     }
     c.chrlen.resize(hdr->n_targets);
     for(int32_t refIndex = 0; refIndex < hdr->n_targets; ++refIndex) c.chrlen[refIndex] = hdr->target_len[refIndex];
     bam_hdr_destroy(hdr);
     hts_idx_destroy(idx);
     sam_close(samfile);
   }
   c.nchr = c.chrlen.size();

   // Check input VCF file
   if (vm.count("vcffile")) {
     if (!(boost::filesystem::exists(c.vcffile) && boost::filesystem::is_regular_file(c.vcffile) && boost::filesystem::file_size(c.vcffile))) {
       std::cerr << "Input VCF/BCF file is missing: " << c.vcffile.string() << std::endl;
       return 1;
     }
     htsFile* ifile = bcf_open(c.vcffile.string().c_str(), "r");
     if (ifile == NULL) {
       std::cerr << "Fail to open file " << c.vcffile.string() << std::endl;
       return 1;
     }
     bcf_hdr_t* hdr = bcf_hdr_read(ifile);
     if (hdr == NULL) {
       std::cerr << "Fail to open index file " << c.vcffile.string() << std::endl;
       return 1;
     }
     bcf_hdr_destroy(hdr);
     bcf_close(ifile);
     c.hasVcfFile = true;
   } else c.hasVcfFile = false;
   
   // Run Tegua
   c.aliscore = DnaScore<int>(3, -2, -3, -1);
   c.flankQuality = 0.9;
   c.minimumFlankSize = 50;
   c.indelsize = 10000;
   return runTegua(c);
 }

}

#endif
