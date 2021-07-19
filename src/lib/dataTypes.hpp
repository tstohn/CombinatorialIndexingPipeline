
#include <iostream>
#include <set>
#include <string>
#include <string.h>
#include <cstdio>

/**
 *    small functionality of a char* set to not use any duplicate strings
 *    
 * */

class UniqueCharSet
{

   struct CharPtrComparator {
      bool operator()(const char* left, const char* right) const {
         return ((left != nullptr) && (right != nullptr) && (strcmp(left, right) < 0));
      }
   };

   public:

      ~UniqueCharSet()
      {
         clearUniqueSet();
      }

      void printSet() 
      {
         for(auto it = charPtrSet.begin(); it != charPtrSet.end(); ++it)
            std::cout << *it << "\n";
      }

      const char* getUniqueChar(const char* k)
      {
         std::set<const char*>::iterator idx = charPtrSet.find(k);
         if(idx != charPtrSet.end())
         {
            return *idx;
         }
         else
         {
            const char* x = insertElement(k);
            return(x);
         }
      }

      const char* insertElement(const char* k) 
      {
         if(!k) {
            std::cout << "Unable to insert char*: \'" << k << "\'\n";
            exit(EXIT_FAILURE);
         }

         char* key = new char[strlen(k)];
         strcpy(key, k);
         charPtrSet.insert(key);

         return(key);
      }

      void clearUniqueSet() 
      {
         for(auto it = charPtrSet.begin(); it != charPtrSet.end(); ++it) 
         {
            const char* key = *it;

            if(key) {
               delete [] key;
               key = nullptr;
            }
         }
         charPtrSet.clear();
      }

   private:
      std::set<const char*, CharPtrComparator> charPtrSet;

};