/**
 * @file   query_processor.cc
 * @author Stavros Papadopoulos <stavrosp@csail.mit.edu>
 *
 * @section LICENSE
 *
 * The MIT License
 * 
 * @copyright Copyright (c) 2014 Stavros Papadopoulos <stavrosp@csail.mit.edu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * @section DESCRIPTION
 *
 * This file implements the QueryProcessor class.
 */
  
#include "query_processor.h"
#include "utils.h"
#include <stdio.h>
#include <typeinfo>
#include <stdlib.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/uio.h>
#include <assert.h>
#include <algorithm>
//#include <parallel/algorithm>
#include <functional>
#include <math.h>
#include <string.h>

/******************************************************
********************* CONSTRUCTORS ********************
******************************************************/

QueryProcessor::QueryProcessor(StorageManager* storage_manager) 
    : storage_manager_(storage_manager) {
}

void QueryProcessor::export_to_csv(
    const std::string& array_name, const std::string& filename) const { 
  // Open array in read mode
  int ad = storage_manager_->open_array(array_name, "r");
  if(ad == -1)
    throw QueryProcessorException(std::string("Cannot open array ") +
                                  array_name + "."); 

  // For easy reference
  const ArraySchema* array_schema = storage_manager_->get_array_schema(ad); 
  int attribute_num = array_schema->attribute_num();
  const std::type_info& coords_type = *(array_schema->type(attribute_num));
 
  // Invoke the proper templated function
  if(coords_type == typeid(int)) 
    export_to_csv<int>(ad, filename); 
  else if(coords_type == typeid(int64_t)) 
    export_to_csv<int64_t>(ad, filename); 
  else if(coords_type == typeid(float)) 
    export_to_csv<float>(ad, filename); 
  else if(coords_type == typeid(double)) 
    export_to_csv<double>(ad, filename); 

  // Clean up
  storage_manager_->close_array(ad); 
}

template<class T>
void QueryProcessor::export_to_csv(
    int ad, const std::string& filename) const { 
  // For easy reference
  const ArraySchema* array_schema = storage_manager_->get_array_schema(ad); 

  // Prepare CSV file
  CSVFile csv_file;
  csv_file.open(filename, "w");

  // Prepare cell iterators
  StorageManager::Array::const_cell_iterator<T> cell_it = 
      storage_manager_->begin<T>(ad);

  // Write cells into the CSV file
  for(; !cell_it.end(); ++cell_it) 
    csv_file << cell_to_csv_line<T>(*cell_it, array_schema); 

  // Clean up 
  csv_file.close();
}

void QueryProcessor::subarray(
    const std::string& array_name, 
    const std::vector<double>& range,
    const std::string& result_array_name) const { 
  // Open array in read mode
  int ad = storage_manager_->open_array(array_name, "r");
  if(ad == -1)
    throw QueryProcessorException(std::string("Cannot open array ") +
                                  array_name + "."); 

  // For easy reference
  const ArraySchema* array_schema = storage_manager_->get_array_schema(ad); 
  int attribute_num = array_schema->attribute_num();
  int dim_num = array_schema->dim_num();
  const std::type_info& coords_type = *(array_schema->type(attribute_num));

  // Check range size
  if(range.size() != 2*dim_num)
    throw QueryProcessorException("Range dimensionality does not agree with"
                                  " number of dimensions in the array schema");

  // Create result array schema
  ArraySchema* result_array_schema = array_schema->clone(result_array_name); 

  // Define result array
  storage_manager_->define_array(result_array_schema);

  // Open result array in write mode
  int result_ad = storage_manager_->open_array(result_array_name, "w");
  if(result_ad == -1)
    throw QueryProcessorException(std::string("Cannot open array ") +
                                  result_array_name + "."); 

  // Invoke the proper templated function
  if(coords_type == typeid(int)) { 
    int* new_range = new int[2*dim_num]; 
    convert(&range[0], new_range, 2*dim_num);
    subarray(ad, new_range, result_ad); 
    delete [] new_range;
  } else if(coords_type == typeid(int64_t)) { 
    int64_t* new_range = new int64_t[2*dim_num]; 
    convert(&range[0], new_range, 2*dim_num);
    subarray(ad, new_range, result_ad); 
    delete [] new_range;
 } else if(coords_type == typeid(float)) { 
    float* new_range = new float[2*dim_num]; 
    convert(&range[0], new_range, 2*dim_num);
    subarray(ad, new_range, result_ad); 
    delete [] new_range;
  } else if(coords_type == typeid(double)) { 
    double* new_range = new double[2*dim_num]; 
    convert(&range[0], new_range, 2*dim_num);
    subarray(ad, new_range, result_ad); 
    delete [] new_range;
  }

  // Clean up
  delete result_array_schema;
  storage_manager_->close_array(ad); 
  storage_manager_->close_array(result_ad); 
}

template<class T>
void QueryProcessor::subarray(int ad, const T* range, int result_ad) const { 
  // Prepare cell iterator
  StorageManager::Array::const_cell_iterator<T> cell_it = 
      storage_manager_->begin<T>(ad, range);

  // Write cells into the CSV file
  for(; !cell_it.end(); ++cell_it) 
    storage_manager_->write_cell_sorted<T>(result_ad, *cell_it); 
}

template<class T>
inline
CSVLine QueryProcessor::cell_to_csv_line(
    const void* cell, const ArraySchema* array_schema) const {
  // For easy reference
  int dim_num = array_schema->dim_num();
  int attribute_num = array_schema->attribute_num();
  bool var_size = (array_schema->cell_size() == VAR_SIZE);
  size_t offset;
  int val_num;

  // Prepare a CSV line
  CSVLine csv_line;

  // Append coordinates first
  const void* coords = cell;
  const std::type_info& coords_type = *(array_schema->type(attribute_num));
  for(int i=0; i<dim_num; ++i)
    csv_line << static_cast<const T*>(coords)[i];

  offset = array_schema->cell_size(attribute_num);
  if(var_size)
    offset += sizeof(size_t);

  // Append attribute values next
  for(int i=0; i<attribute_num; ++i) {
    const std::type_info& attr_type = *(array_schema->type(i));

    if(array_schema->val_num(i) == VAR_SIZE) {
      memcpy(&val_num, static_cast<const char*>(cell) + offset, sizeof(int));
      offset += sizeof(int);
      if(attr_type != typeid(char)) 
        csv_line << val_num;
    } else {
      val_num = array_schema->val_num(i);
    }

    if(attr_type == typeid(char)) {
      std::string v;
      v.resize(val_num);
      memcpy(&v[0], static_cast<const char*>(cell) + offset, val_num);

      if(v[0] == NULL_CHAR)
        csv_line << NULL_VALUE;
      else if(v[0] == DEL_CHAR)
        csv_line << DEL_VALUE;
      else 
        csv_line << v;

      offset += val_num * sizeof(char);
    } else if(attr_type == typeid(int)) { 
      const void* cell_tmp = static_cast<const char*>(cell) + offset;
      const int* v = static_cast<const int*>(cell_tmp);
      for(int j=0; j<val_num; ++j) {
        if(v[j] == NULL_INT)
          csv_line << NULL_VALUE;
        else if(v[j] == DEL_INT)
          csv_line << DEL_VALUE;
        else 
          csv_line << v[j];
        offset += sizeof(int);
      }
    } else if(attr_type == typeid(int64_t)) { 
      const void* cell_tmp = static_cast<const char*>(cell) + offset;
      const int64_t* v = static_cast<const int64_t*>(cell_tmp);
      for(int j=0; j<val_num; ++j) {
        if(v[j] == NULL_INT64_T)
          csv_line << NULL_VALUE;
        else if(v[j] == DEL_INT64_T)
          csv_line << DEL_VALUE;
        else 
          csv_line << v[j];
        offset += sizeof(int64_t);
      }    
    } else if(attr_type == typeid(float)) { 
      const void* cell_tmp = static_cast<const char*>(cell) + offset;
      const float* v = static_cast<const float*>(cell_tmp);
      for(int j=0; j<val_num; ++j) {
        if(v[j] == NULL_FLOAT)
          csv_line << NULL_VALUE;
        else if(v[j] == DEL_FLOAT)
          csv_line << DEL_VALUE;
        else 
          csv_line << v[j];
        offset += sizeof(float);
      }
    } else if(attr_type == typeid(double)) { 
      const void* cell_tmp = static_cast<const char*>(cell) + offset;
      const double* v = static_cast<const double*>(cell_tmp);
      for(int j=0; j<val_num; ++j) {
        if(v[j] == NULL_DOUBLE)
          csv_line << NULL_VALUE;
        else if(v[j] == DEL_DOUBLE)
          csv_line << DEL_VALUE;
        else 
          csv_line << v[j];
        offset += sizeof(double);
      }
    }
  }

  return csv_line;
}
