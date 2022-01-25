#include "BarcodeProcessingHandler.hpp"

#include <unordered_set>
#include <unordered_map>
#include <set>

double calcualtePercentages(std::vector<unsigned long long> groups, int num, double perc)
{
    double readCount = perc * num;
    int sumOfReads = 0;
    int sumBCs = 0;
    std::sort(groups.begin(), groups.end(), std::greater<unsigned long long>());
    for(auto el : groups)
    {
        sumOfReads += el;
        ++sumBCs;
        if(double(sumOfReads) >= readCount)
        {
            return((double)sumBCs/groups.size());
        }
    }
    assert(groups.size() != 0);
    return(-1);
}

void generateBarcodeDicts(std::string barcodeFile, std::string barcodeIndices, NBarcodeInformation& barcodeIdData, 
                          std::vector<std::string>& proteinDict, const int& protIdx, 
                          std::vector<std::string>* treatmentDict, const int& treatmentIdx)
{
    //parse barcode file into a vector of a vector of all sequences
    std::vector<std::vector<std::string> > barcodeList;
    std::ifstream barcodeFileStream(barcodeFile);
    for(std::string line; std::getline(barcodeFileStream, line);)
    {
        std::string delimiter = ",";
        std::string seq;
        size_t pos = 0;
        std::vector<std::string> seqVector;
        while ((pos = line.find(delimiter)) != std::string::npos) 
        {
            seq = line.substr(0, pos);
            line.erase(0, pos + 1);
            for (char const &c: seq) {
                if(!(c=='A' | c=='T' | c=='G' |c=='C' |
                        c=='a' | c=='t' | c=='g' | c=='c'))
                        {
                        std::cerr << "PARAMETER ERROR: a barcode sequence in barcode file is not a base (A,T,G,C)\n";
                        if(c==' ' | c=='\t' | c=='\n')
                        {
                            std::cerr << "PARAMETER ERROR: Detected a whitespace in sequence; remove it to continue!\n";
                        }
                        exit(1);
                        }
            }
            seqVector.push_back(seq);
        }
        seq = line;
        for (char const &c: seq) {
            if(!(c=='A' || c=='T' || c=='G' || c=='C' ||
                    c=='a' || c=='t' || c=='g' || c=='c'))
                    {
                    std::cerr << "PARAMETER ERROR: a barcode sequence in barcode file is not a base (A,T,G,C)\n";
                    if(c==' ' || c=='\t' || c=='\n')
                    {
                        std::cerr << "PARAMETER ERROR: Detected a whitespace in sequence; remove it to continue!\n";
                    }
                    exit(1);
                    }
        }
        seqVector.push_back(seq);
        barcodeList.push_back(seqVector);
        seqVector.clear();
    }
    barcodeFileStream.close();

    //parse the indices of CI-barcodes (the index of the lines that store CI-barcodes)
    std::stringstream ss;
    ss.str(barcodeIndices);
    while(ss.good())
    {
        std::string substr;
        getline(ss, substr, ',' );
        barcodeIdData.NBarcodeIndices.push_back(stoi(substr));
    }

    //write a mapping of barcode sequence to a unique number for each
    //CI barcoding round
    for(const int& i : barcodeIdData.NBarcodeIndices)
    {
        int barcodeCount = 0;
        std::unordered_map<std::string, int> barcodeMap;
        //for all options of this barcode
        for(const std::string& barcodeEntry : barcodeList.at(i))
        {
            barcodeMap.insert(std::pair<std::string, int>(barcodeEntry,barcodeCount));
            ++barcodeCount;
        }
        barcodeIdData.barcodeIdDict.push_back(barcodeMap);
    }

    barcodeIdData.NTreatmentIdx = treatmentIdx;
    barcodeIdData.NAbIdx = protIdx;

    proteinDict = barcodeList.at(protIdx);

    if(treatmentDict != nullptr)
    {
        *treatmentDict = barcodeList.at(treatmentIdx);
    }

}

void BarcodeProcessingHandler::parseFile(const std::string fileName, const int& thread)
{
    int totalReads = totalNumberOfLines(fileName);
    int currentReads = 0;
    //open gz file
    if(!endWith(fileName,".gz"))
    {
        std::cerr << "Input file must be gzip compressed\n";
        exit(EXIT_FAILURE);
    }
    std::ifstream file(fileName, std::ios_base::in | std::ios_base::binary);
    boost::iostreams::filtering_streambuf<boost::iostreams::input> inbuf;
    inbuf.push(boost::iostreams::gzip_decompressor());
    inbuf.push(file);
    std::istream instream(&inbuf);
    
    parseBarcodeLines(&instream, totalReads, currentReads);
    file.close();
}

void BarcodeProcessingHandler::parseBarcodeLines(std::istream* instream, const int& totalReads, int& currentReads)
{
    std::string line;
    std::cout << "READING ALL LINES INTO MEMORY\n";
    int count = 0;
    int elements = 0; //check that each row has the correct number of barcodes
    while(std::getline(*instream, line))
    {
        //for the first read check the positions in the string that refer to CIBarcoding positions
        if(currentReads==0){
            ++currentReads; 
            getBarcodePositions(line, elements);
            continue;
        }
        addFastqReadToUmiData(line, elements);   

        double perc = currentReads/ (double)totalReads;
        ++currentReads;
        printProgress(perc);        
    }
    printProgress(1);
    std::cout << "\n";
}

void BarcodeProcessingHandler::addFastqReadToUmiData(const std::string& line, const int& elements)
{
    //split the line into barcodes
    std::vector<std::string> result;
    std::stringstream ss;
    ss.str(line);
    std::string substr;

    while(getline( ss, substr, '\t' ))
    {
        if(substr != ""){result.push_back( substr );}
    }
    if(result.size() != elements)
    {
        std::cerr << "Error in barcode file, following row has not the correct number of sequences: " << line << "\n";
        exit(EXIT_FAILURE);
    }

    //hand over the UMI string, ab string, singleCellstring (concatenation of CIbarcodes)
    std::vector<std::string> ciBarcodes;
    for(int i : fastqReadBarcodeIdx)
    {
        ciBarcodes.push_back(result.at(i));
    }
    std::string singleCellIdx = generateSingleCellIndexFromBarcodes(ciBarcodes);
    std::string proteinName = rawData.getProteinName(result.at(abIdx));
    
    std::string treatment = "";

    if(treatmentIdx != INT_MAX)
    {
        treatment = rawData.getTreatmentName(result.at(treatmentIdx));
    }

    rawData.add(result.at(umiIdx), proteinName, singleCellIdx, treatment);
}

std::string BarcodeProcessingHandler::generateSingleCellIndexFromBarcodes(std::vector<std::string> ciBarcodes)
{
    std::string scIdx;

    for(int i = 0; i < ciBarcodes.size(); ++i)
    {
        std::string barcodeAlternative = ciBarcodes.at(i);
        int tmpIdx = varyingBarcodesPos.barcodeIdDict.at(i)[barcodeAlternative];
        scIdx += std::to_string(tmpIdx);
        if(i < ciBarcodes.size() - 1)
        {
            scIdx += ".";
        }
    }

    return scIdx;
}

void BarcodeProcessingHandler::getBarcodePositions(const std::string& line, int& barcodeElements)
{
    std::vector<std::string> result;
    std::stringstream ss;
    ss.str(line);
    int count = 0;
    int variableBarcodeCount = 0;
    std::string substr;
    while(std::getline(ss, substr, '\t'))
    {
        if(substr.empty()){continue;}
        //if substr is only N's
        if(substr.find_first_not_of('N') == std::string::npos)
        {
            //add index for treatment
            if(variableBarcodeCount == varyingBarcodesPos.NTreatmentIdx)
            {
                treatmentIdx = count;
            }
            //add index for combinatorial indexing barcode
            if (std::count(varyingBarcodesPos.NBarcodeIndices.begin(), varyingBarcodesPos.NBarcodeIndices.end(), variableBarcodeCount)) 
            {
                fastqReadBarcodeIdx.push_back(count);
            }
            if(variableBarcodeCount == varyingBarcodesPos.NAbIdx)
            {
                abIdx = count;
            }
            ++variableBarcodeCount;
        }
        else if(substr.find_first_not_of('X') == std::string::npos)
        {
            umiIdx = count;
            umiLength = strlen(substr.c_str());
        }
        result.push_back( substr );
        ++count;
    }
    barcodeElements = count;
    assert(fastqReadBarcodeIdx.size() == varyingBarcodesPos.NBarcodeIndices.size());
    assert(umiIdx != INT_MAX);
    assert(abIdx != INT_MAX);
}

void BarcodeProcessingHandler::markReadsWithNoUniqueUmi(std::vector<dataLinePtr> uniqueUmis,
                                                        std::vector<dataLinePtr>& dataLinesToDelete, 
                                                        unsigned long long& count,
                                                        const unsigned long long& totalCount)
{
    //count how often we see which AB-SC-Treatment combinations for this certain UMI
    std::unordered_map<std::string, unsigned long long> umiCountMap;
    for(unsigned long long i = 0; i < uniqueUmis.size(); ++i)
    {
        std::string uniqueID = std::string(uniqueUmis.at(i)->scID) + std::string(uniqueUmis.at(i)->abName) + std::string(uniqueUmis.at(i)->treatmentName);
        std::unordered_map<std::string, unsigned long long>::iterator umiCountMapIt = umiCountMap.find(uniqueID);
        if( umiCountMapIt != umiCountMap.end())
        {
            ++(umiCountMapIt->second);
        }
        else
        {
            umiCountMap.insert(std::make_pair(uniqueID, 1));
        }
    }

    std::string realSingleCellID;
    bool realSingleCellExists = false;
    for(auto singleCellCountPair : umiCountMap)
    {
        double singleCellPerc = singleCellCountPair.second/umiCountMap.size();
        if( singleCellPerc >= 0.9 )
        {
            realSingleCellID = singleCellCountPair.first;
            realSingleCellExists = true;
        }
    }

    if(!realSingleCellExists)
    {
        //delete all reads
        //for(int i = 0; i < uniqueUmis.size(); ++i)
        //{
            //   {
            //      dataLinesToDelete.push_back(uniqueUmis.at(i));
            // }
        //}
        result.add_removed_reads(uniqueUmis.size());
    }
    else
    {
        //delete only the <=10% 'false' reads
        unsigned long long readCountToDelete = 0;
        for(int i = 0; i < uniqueUmis.size(); ++i)
        {
            std::string uniqueID = std::string(uniqueUmis.at(i)->scID) + std::string(uniqueUmis.at(i)->abName) + std::string(uniqueUmis.at(i)->treatmentName);
            if(uniqueID != realSingleCellID)
            {
                dataLinesToDelete.push_back(uniqueUmis.at(i));
                ++readCountToDelete;
            }
        }
        result.add_removed_reads(readCountToDelete);
    }

    if( (totalCount >= 100) && (count % (totalCount / 100) == 0) )
    {
        statusUpdateLock.lock();
        double perc = count/ (double) totalCount;
        printProgress(perc);
        ++count;
        statusUpdateLock.unlock();
    }
}


void BarcodeProcessingHandler::count_umi_occurence(std::vector<int>& positionsOfSameUmi, 
                                                   umiCount& umiLineTmp,
                                                   const std::vector<dataLinePtr>& allScAbCounts,
                                                   const int& umiMismatches,
                                                   const int& lastIdx)
{
    for(int j = 0; j < (allScAbCounts.size() - 1); ++j)
    {
        //calling outputSense algorithm, much faster than levenshtein O(e*max(m,n))
        //however is recently implemented without backtracking
        //before umiMismatches was increased by the length difference between the two UMIs 
        //(no longer done, those deletion should probably be considered as part of the allowed umiMismatches)
        const char* umia = allScAbCounts.at(lastIdx)->umiSeq;
        const char* umib = allScAbCounts.at(j)->umiSeq;
        int dist = INT_MAX;
        int start = 0;
        int end = 0;
        bool similar = outputSense(umia, umib, umiMismatches, dist);

        //if mismatches are within range, change UMI seq
        //the new 'correct' UMI sequence is the one of umiLength, if both r of
        //same length, its the first occuring UMI
        if(dist <= umiMismatches)
        {
            //UMIs are not corrected in rawData (the rawData keeps the 'wrong' umi sequences)
            //they could be changed by calling 'changeUmi'
            positionsOfSameUmi.push_back(j);     
            ++umiLineTmp.abCount; // increase count for this UMI
        }
    }
}

void BarcodeProcessingHandler::count_abs_per_single_cell(const int& umiMismatches, std::vector<dataLinePtr> uniqueAbSc,
                                                        const std::vector<dataLinePtr>& dataLinesToDelete, 
                                                        unsigned long long& count,
                                                        const unsigned long long& totalCount)
{
   //correct for UMI mismatches and fill the AbCountvector
    //iterate through same AbScIdx, calculate levenshtein dist for all UMIs and match those with a certain number of mismatches

        std::cout << __LINE__ << "\n";
        //all dataLines for this AB SC combination
        std::vector<dataLinePtr> scAbCounts = uniqueAbSc;
        
        //data structures to be filled for the UMI and AB count
        scAbCount abLineTmp; // we fill only this one AB SC count
        umiCount umiLineTemplate; //we fill several UMIcounts for every UMI of this AB-SC combination
        
        abLineTmp.scID = umiLineTemplate.scID = uniqueAbSc.at(0)->scID;
        abLineTmp.abName = umiLineTemplate.abName = uniqueAbSc.at(0)->abName;
        abLineTmp.treatment = umiLineTemplate.treatment = uniqueAbSc.at(0)->treatmentName;
        std::cout << __LINE__ << "\n";

        //we take always last element in vector of read of same AB and SC ID
        //then store all reads wwhere UMIs are within distance, and delete those line, and sum up the AB count by one
        while(!scAbCounts.empty())
        {
            std::cout << __LINE__ << "\n";

            dataLinePtr lastAbSc = scAbCounts.back();
            int lastIdx = scAbCounts.size() - 1;
            umiCount umiLineTmp = umiLineTemplate;
            //check if we have to delete element anyways, since it is not a unique UMI
            //in this case we simply delete this line and check the next one
                    std::cout << __LINE__ << "\n";

            if(checkIfLineIsDeleted(lastAbSc, dataLinesToDelete))
            {
                scAbCounts.pop_back();
                continue;
            }

            //if in last element
            if(scAbCounts.size() == 1)
            {
                ++abLineTmp.abCount;
                umiLineTmp.umi = lastAbSc->umiSeq;
                result.add_umi_count(umiLineTmp);
                scAbCounts.pop_back();
                break;
            }
                    std::cout << __LINE__ << "\n";

            //otherwise conmpare all and mark the ones to delete
            std::vector<int> deletePositions;
            deletePositions.push_back(lastIdx);
            ++umiLineTmp.abCount; //count the first occurence
            //count all occurences of the last UMI for this AB-SC
                    std::cout << __LINE__ << "\n";

            count_umi_occurence(deletePositions, umiLineTmp, scAbCounts, umiMismatches, lastIdx);
            std::cout << __LINE__ << "\n";

            //ADD UMI if exists
            if(umiLineTmp.abCount > 0)
            {
                umiLineTmp.umi = lastAbSc->umiSeq;
                result.add_umi_count(umiLineTmp);
            }
                    std::cout << __LINE__ << "\n";

            //delte all same UMIs
            //positions have to be sorted, we first add the last read, and then sequencially all reads with the same UMI
            //starting at the front. But we have to delete from the end to not run into segfaults
            sort(deletePositions.begin(), deletePositions.end());
            for(int posIdx = (deletePositions.size() - 1); posIdx >= 0; --posIdx)
            {
                std::cout << "posIdx: "<< posIdx << "\n";
                                std::cout << "Absc size: "<< scAbCounts.size() << "\n";
                                std::cout << "del size: "<< deletePositions.size() << "\n";

                int pos = deletePositions.at(posIdx);
                std::cout << "del pos in vec: " << pos << "\n";
                scAbCounts.erase(scAbCounts.begin() + pos);
                        std::cout << __LINE__ << "\n";

            }
            std::cout << __LINE__ << "\n";

            //increase AB count for this one UMI
            ++abLineTmp.abCount;
        }

        //add the data to AB counts if it exists
        if(abLineTmp.abCount>0)
        {
            result.add_ab_count(abLineTmp);
        }
        std::cout << __LINE__ << "\n";

        if((totalCount >= 100) && (count % (totalCount / 100) == 0))
        {
            statusUpdateLock.lock();
            double perc = count/ (double) totalCount;
            printProgress(perc);
            ++count;
            statusUpdateLock.unlock();
        }
}

void BarcodeProcessingHandler::processBarcodeMapping(const int& umiMismatches, const int& thread)
{
    boost::asio::thread_pool pool(thread); //create thread pool

    //implement thread safe update functions for the data
    //Abcounts Umicounts UmiLog

    //check all UMIs and keep thier reads only if they are for >90% a unique scID, ABname, treatmentname
    std::vector<dataLinePtr> dataLinesToDelete; // the dataLines that contain reads that have to be deleted (several CIbarcodes, AB, treatments for same UMI)
    unsigned long long umiCount = 0; //using atomic<int> as thread safe read count
    unsigned long long totalCount = rawData.getUniqueUmis().size();
    for(std::pair<const char*, std::vector<dataLinePtr>> uniqueUmi : rawData.getUniqueUmis())
    {
        //careful: uniqueUmi goe sout of scope after enqueuing, therefore just copied...
        boost::asio::post(pool, std::bind(&BarcodeProcessingHandler::markReadsWithNoUniqueUmi, this, 
                                          (uniqueUmi.second), std::ref(dataLinesToDelete), 
                                          std::ref(umiCount), std::cref(totalCount)));
    }
    pool.join();

    //generate ABcounts per single cell:
    umiCount = 0; //using atomic<int> as thread safe read count
    totalCount = rawData.getUniqueAbSc().size();
    boost::asio::thread_pool pool2(thread); //create thread pool
    for(std::pair<const char*, std::vector<dataLinePtr>> abSc : rawData.getUniqueAbSc())
    {
        //as above: abSc is copied only
        boost::asio::post(pool2, std::bind(&BarcodeProcessingHandler::count_abs_per_single_cell, this, 
                                          std::cref(umiMismatches), abSc.second, 
                                          std::cref(dataLinesToDelete), std::ref(umiCount), 
                                          std::cref(totalCount) ));
    }
    pool2.join();
}

bool BarcodeProcessingHandler::checkIfLineIsDeleted(const dataLinePtr& line, const std::vector<dataLinePtr>& dataLinesToDelete)
{
    if(std::find(dataLinesToDelete.begin(), dataLinesToDelete.end(),line) != dataLinesToDelete.end())
    {
        return true;
    }
    return false;
}
/*
void BarcodeProcessingHandler::writeLog(std::string output)
{
    //WRITE INTO FILE
    std::ofstream outputFile;
    std::size_t found = output.find_last_of("/");
    if(found == std::string::npos)
    {
        output = "STATS" + output;
    }
    else
    {
        output = output.substr(0,found) + "/" + "STATS" + output.substr(found+1);
    }
    outputFile.open (output);



    outputFile.close();

}*/

void BarcodeProcessingHandler::writeUmiCorrectedData(const std::string& output)
{
    std::ofstream outputFile;
    std::size_t found = output.find_last_of("/");

    //STORE RAW UMI CORRECTED DATA
    std::string umiOutput = output;
    if(found == std::string::npos)
    {
        umiOutput = "UMI" + output;
    }
    else
    {
        umiOutput = output.substr(0,found) + "/" + "UMI" + output.substr(found+1);
    }
    outputFile.open (umiOutput);
    outputFile << "UMI" << "\t" << "AB" << "\t" << "SingleCell_ID" << "\t" << "TREATMENT" << "\t" << "UMI_COUNT" << "\n"; 
    for(umiCount line : result.get_umi_data())
    {
        outputFile << line.umi << "\t" << line.abName << "\t" << line.scID << "\t" << line.treatment << "\t" << line.abCount << "\n"; 
    }
    outputFile.close();

    //STORE AB COUNT DATA
    std::string abOutput = output;
    if(found == std::string::npos)
    {
        abOutput = "AB" + output;
    }
    else
    {
        abOutput = output.substr(0,found) + "/" + "AB" + output.substr(found+1);
    }
    outputFile.open (abOutput);
        outputFile << "AB_BARCODE" << "\t" << "SingleCell_BARCODE" << "\t" << "AB_COUNT" << "\t" << "TREATMENT" << "\n"; 

    //outputFile << "AB" << "\t" << "SingleCell_ID" << "\t" << "TREATMENT" << "\t" << "AB_COUNT" << "\n"; 
    for(scAbCount line : result.get_ab_data())
    {
                outputFile << line.abName << "\t" << line.scID << "\t" << line.abCount << "\t" << line.treatment << "\n"; 

        //outputFile << line.abName << "\t" << line.scID << "\t" << line.treatment << "\t" << line.abCount << "\n"; 
    }
    outputFile.close();
}