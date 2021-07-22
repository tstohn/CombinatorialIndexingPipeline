#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <zlib.h>
#include <vector>
#include <thread>
#include <pthread.h>
#include <unordered_map>
#include <sstream>
#include <climits>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include "dataTypes.hpp"

struct dataLine
{
    const char* umi_seq;
    const char* ab_seq;
    const char* cell_seq;
};
typedef std::shared_ptr<dataLine> dataLinePtr;
 
struct CIBarcode
{
    std::vector<std::unordered_map<std::string, int> > barcodeIdDict; // map of barcode to id, maps are in order of their occurence in the fastqRead
    // ids of the CI barcode within the barcode file (includes only barcodes for variable sequence regions)
    std::vector<int> ciBarcodeIndices; // more or less only of temporary usage, during generation of barcode map vector
};

class UmiData
{
    public:

        // add a dataLines to the vector
        void add(std::string& umiStr, std::string& abStr, std::string& singleCellStr)
        {
            //get unique pointer for all three string
            dataLine line;
            uniqueChars.getUniqueChar(umiStr.c_str());
            line.umi_seq = uniqueChars.getUniqueChar(umiStr.c_str());
            line.ab_seq = uniqueChars.getUniqueChar(abStr.c_str());
            line.cell_seq = uniqueChars.getUniqueChar(singleCellStr.c_str());

            //make a dataLinePtr from those unique string
            dataLinePtr linePtr(std::make_shared<dataLine>(line));

            //add it to our dataStructure (3 entries have to be set)

            addDataLine(linePtr);
        }

        //return all dataLines of this specific UMI
        std::vector<dataLinePtr> getDataLinesForUmi(const char* umi_seq)
        {
            std::vector<dataLinePtr> returnLines;

            return returnLines;
        }
        //return all dataLines of this specific AB-SingleCell combination
        std::vector<dataLinePtr> getDataLinesForABSingleCell(const char* abBarcode, const char* singleCellBarcode)
        {
            std::vector<dataLinePtr> returnLines;

            return returnLines;
        }

        //return functions for our data, based on positions, UMI or AB/ SC barcodes
        inline const std::vector<dataLinePtr> getData() const
        {
            return data;
        }
        inline dataLinePtr getDataAt(int pos) const
        {
            return data.at(pos);
        }
        inline std::vector<dataLinePtr> getDataWithUmi(const char* umi) const
        {
            return positionsOfUmi.at(umi);
        }
        inline std::vector<dataLinePtr> getDataWithAbSc(const char* ab, const char* sc) const
        {
            std::string abScIdxStr = std::string((ab)) + std::string((sc));
            return positonsOfABSingleCell.at(abScIdxStr.c_str());
        }
        inline std::vector<dataLinePtr> getDataWithAbSc(const char* absc) const
        {
            return positonsOfABSingleCell.at(absc);
        }
        inline std::vector<std::pair<const char*, int> > getNumberOfUniqueUmis() const
        {
            std::vector<std::pair<const char*, int> > uniqueUmiNums;
            for(auto mapIdx : positionsOfUmi)
            {
                uniqueUmiNums.emplace_back(std::make_pair(mapIdx.first, mapIdx.second.size()));
            }
            return uniqueUmiNums;
        }
        inline std::vector<std::pair<const char*, int> > getNumberOfUniqueAbSc() const
        {
            std::vector<std::pair<const char*, int> > uniqueAbScNums;
            for(auto mapIdx : positonsOfABSingleCell)
            {
                uniqueAbScNums.emplace_back(std::make_pair(mapIdx.first, mapIdx.second.size()));
            }
            return uniqueAbScNums;
        }

    private:

        void addDataLine(dataLinePtr line)
        {
            //1.) ADD LINE
            data.push_back(line);

            //2.) INSERT UMI POSITIONS
            //if umi already exists also store this new position
            if(positionsOfUmi.find(line->umi_seq) == positionsOfUmi.end())
            {
                std::vector<dataLinePtr> vec;
                vec.push_back(line);
                positionsOfUmi.insert(std::make_pair(line->umi_seq, vec));
            }
            //if not add this new umi with this actual position to map
            else
            {
                positionsOfUmi[line->umi_seq].push_back(line);
            }

            // 3.) INSERT ABSC POSITIONS
            //same for AbSingleCell
            std::string abScIdxStr = std::string((line->ab_seq)) + std::string((line->cell_seq));
            
            const char* abScIdxChar = uniqueChars.getUniqueChar(abScIdxStr.c_str());
            if(positonsOfABSingleCell.find(abScIdxChar) == positonsOfABSingleCell.end())
            {
                std::vector<dataLinePtr> vec;
                vec.push_back(line);
                positonsOfABSingleCell.insert(std::make_pair(abScIdxChar, vec));
            }
            //if not add this new umi with this actual position to map
            else
            {
                positonsOfABSingleCell[abScIdxChar].push_back(line);
            }
        }

        std::string uniqueAbSingleCellId(std::string abId, std::string singleCellId)
        {
            std::string id = abId + singleCellId;
            return id;
        }

        std::vector<dataLinePtr> data;
        std::unordered_map<const char*, std::vector<dataLinePtr>, CharHash, CharPtrComparator> positionsOfUmi;
        std::unordered_map<const char*, std::vector<dataLinePtr>, CharHash, CharPtrComparator> positonsOfABSingleCell;

        //all the string inside this class are stored only once, 
        //set of all the unique barcodes we use, and we only pass pointers to those
        UniqueCharSet uniqueChars;

};

class UmiDataParser
{


    public:

        UmiDataParser(CIBarcode barcodeIdData) : barcodeDict(barcodeIdData){}

        void parseFile(const std::string fileName, const int& thread);
        void writeStats(std::string output);
        void writeUmiCorrectedData(const std::string& output);

    private:

        //parse the file, store each line in our data structure
        void addFastqReadToUmiData(const std::string& line);
        void parseBarcodeLines(std::istream* instream, const int& totalReads, int& currentReads);

        //fill the CIBarcode dict: mapping of barcode alternatives to an unique idx
        void getCiBarcodeInWholeSequence(const std::string& line);
        //map all the barcodes of CI to a unique 'number' string as SingleCellIdx
        std::string generateSingleCellIndexFromBarcodes(std::vector<std::string> ciBarcodes);

        //data structure storing lines with: UMI, AB_id, SingleCell_id
        UmiData data;
        //Dictionary used to generate the dataLines, maps for each barcode in teh sequence all
        //possibilities to an idx
        CIBarcode barcodeDict;
        //
        std::vector<int> fastqReadBarcodeIdx; // ids of the CI barcode within the whole string of all barcodes
        int abIdx = INT_MAX;
        int umiIdx = INT_MAX;
};
