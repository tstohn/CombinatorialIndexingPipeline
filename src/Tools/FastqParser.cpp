#include <iostream>
#include <fstream>
#include <string>
#include <zlib.h>
#include <regex>
#include <thread>

#include "Barcode.hpp"

/** @param:
 *  sequence pattern: could be a sequence of constant and variable nucleotides: e.g.: [xxxxx][AGCGTACGCGAGT][xxxxx][AAGCGtAGCTTC][xxxxx] 
 *  mismatches per sequence in bracket: e.g.: 1,2,1,2,1
 * 
 * 
 **/

//TODO:
/*
make a basetype of barcode:
 - derived classes of constant codes with ONE string and variables one with X strings
 - iterate through barcodes and match them in increasing order

 - if everything matches within the order of mismatches reporta vector of matches

 INPUT: pattern plus mismatches and barcode sequences
 => vector of barcode pointer which inherits to constant and variable (difference std::stirng anf vector of strings)

 OUTPUT: one vector of (vectors: all matches sequences in a row)

PLAN:
- design classes plus a function to calc dist for one or x sequences and return best match
- parse pattern, mismatches, and barcodeList and store in new Baseclasspointer vector


*/

#include "seqtk/kseq.h"

#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>

KSEQ_INIT(gzFile, gzread)

//old data types
typedef std::vector<std::string> BarcodeMapping;
typedef std::vector<BarcodeMapping> BarcodeMappingVector;
using namespace boost::program_options;


struct input{
    std::string inFile;
    std::string outFile;

    std::string wellBarcodes;
    std::string abBarcodes;

    std::string anchor;
    int mb = 1;
    int ma = 2;
    int threads = 5;
};

struct fastqStats{
    int perfectMatches = 0;
    int noMatches = 0;
    int moderateMatches = 0;
};

bool parse_arguments(char** argv, int argc, input& input)
{
    try
    {
        std::string mismatchesString;
        options_description desc("Options");
        desc.add_options()
            ("input,i", value<std::string>(&(input.inFile))->required(), "directory of files or single file in fastq(.gz) format")
            ("output,o", value<std::string>(&(input.outFile))->required(), "output file with all split barcodes")
            ("sequencePattern,p", value<std::string>(&(input.anchor))->required(), "pattern for the sequence to match, \
            every substring that should be matched is enclosed with square brackets. N is a wild card match, a substring \
            should not be a combination of wild card chars and constant chars : [AGCTATCACGTAGC][NNNNNN][AGAGCATGCCTTCAG][NNNNNN]")

            ("barcodeList,b", value<std::string>(&(input.wellBarcodes)), "file with a list of all allowed well barcodes (comma seperated barcodes across several rows)\
            the row refers to the correponding bracket enclosed sequence substring. E.g. for two bracket enclosed substrings in out sequence a possible list could be:\
            AGCTTCGAG,ACGTTCAGG\nACGTCTAGACT,ATCGGCATACG,ATCGCGATC,ATCGCGCATAC")

            ("mismatches,m", value<std::string>(&(mismatchesString))->default_value("1"), "list of mismatches allowed for each bracket enclosed sequence substring. \
            This should be a comma seperated list of numbers for each substring of the sequence enclosed in squared brackets. E.g.: 2,1,2,1,2")
            ("threat,t", value<int>(&(input.threads))->default_value(5), "number of threads")

            ("help,h", "help message");

        variables_map vm;
        store(parse_command_line(argc, argv, desc), vm);

        if(vm.count("help"))
        {
            std::cout << desc << "\n";
            std::cout << "EXAMPLE CALL:\n ./bin/parser -i ./inFile -o ./outFile -p [AGTCAGTC][NNNN] -m 2,1 -t 5\n";
            return false;
        }

        notify(vm);
    }
    catch(std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return false;
    }
    return true;

}

struct levenshtein_value{
        int val = 0;
        int i = 0;
        int j = 0;
        levenshtein_value(int a,int b, int c):val(a), i(b), j(c){}
        levenshtein_value():val(0), i(0), j(0){}
};
levenshtein_value min(levenshtein_value a, levenshtein_value b, bool& first)
{
    if(a.val <= b.val)
    {
        first = true;
        return a;
    }
    return b;
}
bool levenshtein(const std::string seq, std::string anchor, int ma, int mb, int& match_start, int& match_end, int& score)
{
    int i,j,ls,la,t,substitutionValue, deletionValue;
    //stores the lenght of strings s1 and s2
    ls = seq.length();
    la = anchor.length();
    levenshtein_value dist[ls+1][la+1];

    //allow unlimited deletions in beginning; start is zero
    for(i=0;i<=ls;i++) {
        levenshtein_value val(i,i-1,0);
        dist[i][0] = val;
    }
    //deletions in pattern is punished; start is zero
    for(j=0;j<=la;j++) {
        levenshtein_value val(j,0,j-1);
        dist[0][j] = val;
    }
    levenshtein_value val(0,-1,-1);
    dist[0][0] = val;
    for (i=1;i<=ls;i++) 
    {
        for(j=1;j<=la;j++) 
        {
            //Punishement for substitution
            if(seq[i-1] == anchor[j-1]) 
            {
                substitutionValue= 0;
            } 
            else 
            {
                substitutionValue = 1;
            }
            //punishement for deletion (allow deletions on beginnign and end)
            if( j==1 | j==la )
            {
                deletionValue= 0;
            } 
            else 
            {
                deletionValue = 1;
            }

            //determine new value of cell
            levenshtein_value seq_del = dist[i-1][j]; 
            seq_del.val += deletionValue;
            seq_del.i = i-1;
            seq_del.j = j;
            levenshtein_value seq_ins = dist[i][j-1];
            seq_ins.val += 1;
            seq_ins.i = i;
            seq_ins.j = j-1;
            levenshtein_value subst = dist[i-1][j-1];
            subst.val += substitutionValue;
            subst.i = i-1;
            subst.j = j-1;

            //if equal score, prefer: subst > ins > del
            bool firstValueIsMin = false;
            levenshtein_value tmp1 = min(seq_ins, seq_del, firstValueIsMin);
            firstValueIsMin = false;
            levenshtein_value tmp2 = min(subst, tmp1, firstValueIsMin);
            
            dist[i][j] = tmp2;
        }
    }

    //backtracking to find match start and end
    // start and end are defined as first and last match of bases 
    //(deletion, insertion and substitution are not considered, since they could also be part of the adjacent sequences)
    int start;
    int end;
    i = ls;
    j =la;
    bool noEnd = true;
    while(j != 1)
    {

        int iNew = dist[i][j].i;
        int jNew = dist[i][j].j;

        if(dist[i][j].val == dist[iNew][jNew].val & i!=iNew & j!=jNew)
        {
            start = iNew;
        }

        if( (jNew<la) & noEnd)
        {
            noEnd = false;
            end = i;
        }

        i = iNew;
        j = jNew;
        assert(j!=0);
    }
    if(start==0){start = i+1;}

    if((dist[ls][la]).val <= ma)
    {
        score = (dist[ls][la]).val;
        match_start = start;
        match_end = end;
        return true;
    }
    return false;
}

bool split_sequence(const std::string seq, input* input, BarcodeMapping& barcode, fastqStats& stats)
{
    //match regex of line
    std::smatch sm;
    std::string expressionString = "[ACTGN]+([ACTGN]{15,15})([ACTGN]{10,10})" + input->anchor + "([ACTGN]{10,10})[ACTGN]+";
    std::regex expression(expressionString);
    std::string line = seq;
    std::regex_match(line,sm,expression);

    //check of mismatches
    if(sm.size()<4)
    {
        int start = 0, end = 0, score = 0;
        // start in seq is at: start-1, end-start+1
        if(levenshtein(seq, input->anchor, input->ma, input->mb, start, end, score))
        {
            int startIdx = start-1;
            int endIdx = end; // inclusive
            //minor mismatches that are allowed per mb and ma
            ++stats.moderateMatches;
            return true;
        }
        else
        {
            //bigger mismatches
            ++stats.noMatches;
            return false;
        }
    }
    else
    {
        //perfect matches
        assert(sm.size()==4);
        barcode.push_back(sm[1]);
        barcode.push_back(sm[2]);
        barcode.push_back(sm[3]);
        ++stats.perfectMatches;

        return true;
    }

    return false;
}

void write_file(std::string output, BarcodeMappingVector barcodes)
{
    std::ofstream outputFile;
    outputFile.open (output);
    //write header line
    outputFile << "header\n";
    outputFile.close();
}

void generate_barcodes(std::vector<std::string> fastqLines, input* input, BarcodeMappingVector& barcodes, fastqStats& stats)
{
    BarcodeMapping barcode;
    for(const std::string& line : fastqLines)
    {
        if(split_sequence(line, input, barcode, stats))
        {
            barcodes.push_back(barcode);
        }
    }
}

BarcodeMappingVector iterate_over_fastq(input& input)
{
    //read all fastq lines into str vector
    gzFile fp;
    kseq_t *ks;
    fp = gzopen(input.inFile.c_str(),"r");
    if(NULL == fp){
        fprintf(stderr,"Fail to open file: %s\n", input.inFile.c_str());
    }
    ks = kseq_init(fp);
    std::vector<std::string> fastqLines;
    while( kseq_read(ks) >= 0 ){
        fastqLines.push_back(std::string(ks->seq.s));
    }

    //split input lines into thread buckets
    std::vector<std::vector<std::string> > fastqLinesVector;
    int element_number = fastqLines.size() / input.threads;
    std::vector<std::string>::iterator begin = fastqLines.begin();
    for(int i = 0; i < input.threads; ++i)
    {
        std::vector<std::string>::iterator end = ( i==input.threads-1 ? fastqLines.end() : begin + element_number);

        std::vector<std::string> tmpFastqLines(begin, end);
        fastqLinesVector.push_back(tmpFastqLines);
        begin = end;
    }

    //for every bucket call a thread
    //tmp variables to store thread results
    std::vector<BarcodeMappingVector> barcodesThreadList(input.threads);
    std::vector<fastqStats> statsThreadList(input.threads);
    std::vector<std::thread> workers;
    //final data variables
    BarcodeMappingVector barcodeVectorFinal;
    fastqStats fastqStatsFinal;
    for (int i = 0; i < input.threads; ++i) {
        workers.push_back(std::thread(generate_barcodes, fastqLinesVector.at(i), &input, std::ref(barcodesThreadList.at(i)), std::ref(statsThreadList.at(i))));
    }
    for (std::thread &t: workers) 
    {
        if (t.joinable()) {
            t.join();
        }
    }
    //combine thread data
    for (int i = 0; i < input.threads; ++i) 
    {
        barcodeVectorFinal.insert(barcodeVectorFinal.end(), barcodesThreadList.at(i).begin(), barcodesThreadList.at(i).end());
        fastqStatsFinal.perfectMatches += statsThreadList.at(i).perfectMatches;
        fastqStatsFinal.noMatches += statsThreadList.at(i).noMatches;
        fastqStatsFinal.moderateMatches += statsThreadList.at(i).moderateMatches;
    }
    std::cout << "MATCHED: " << fastqStatsFinal.perfectMatches << " | MODERATE MATCH: " << fastqStatsFinal.moderateMatches
              << " | MISMATCHED: " << fastqStatsFinal.noMatches << "\n";
    kseq_destroy(ks);
    gzclose(fp);
    return barcodeVectorFinal;
}

void call_barcode_splitting_for_each_fastq(input input, BarcodeMappingVector& barcodes)
{
    //if directory iterate over all fastqs
    barcodes = iterate_over_fastq(input);

    //add plate number and combine all barcodes

}

int main(int argc, char** argv)
{
    input input;
    BarcodeMappingVector barcodes;
    if(parse_arguments(argv, argc, input))
    {
        call_barcode_splitting_for_each_fastq(input, barcodes);
        write_file(input.outFile, barcodes);
    }
 
    return EXIT_SUCCESS;
}