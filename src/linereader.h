
#ifndef STORAGE_LINEREADER_H
#define STORAGE_LINEREADER_H

#include <my_global.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "mysql_priv.h"

#if MYSQL_VERSION_ID >= 50120
#define byte uchar
#endif

class String;

class LineReader 
{
 public:

  LineReader(const char *path) 
    : 
      path_(path), 
      buffer_(NULL), 
      buffer_size_(0), 
      current_offset_(-1)
  {}
  ~LineReader() 
  { 
    delete[] buffer_; 
  }

  int Open();
  int Opened() const 
  { 
    return buffer_ != NULL; 
  }
  void Advance();
  void CurrentLine(String *into);
  void LineAt(off_t offset, String *into);
  off_t CurrentOffset();
  off_t LastOffset();

private:

  const char *path_;
  byte *buffer_;
  off_t buffer_size_;
  off_t current_offset_;
  off_t _NextLineOffset(off_t whence);

};

#endif /* STORAGE_LINEREADER_H */
