
#include "linereader.h"
#include <my_sys.h>
#include <sql/mysql_priv.h>

int
LineReader::Open() 
{
  File f= my_open(path_, O_RDONLY, MYF(0));

  if (f == -1)
  {
    return HA_ERR_CRASHED_ON_USAGE;
  }

  int buffsize= 65536;
  int cursize= 0;
  byte *file_data= new(std::nothrow) byte[buffsize];

  while (1) 
  {
    byte tmpbuf[65536];
    uint size= my_read(f, tmpbuf, sizeof(tmpbuf), MYF(0));
    if (size == 0)
    {
      break;
    }

    if (size + cursize > buffsize) 
    {
      byte *tmp= new(std::nothrow) byte[2 * buffsize];
      buffsize*= 2;
      memmove(tmp, file_data, cursize);
      delete [] file_data;
      file_data= tmp;
    }

    memmove(file_data + cursize, tmpbuf, size);
    cursize+= size;
  }
  buffer_= file_data;

  buffer_size_= cursize;
  current_offset_= 0;

  return 0;
}


void
LineReader::Advance() 
{
  current_offset_= _NextLineOffset(current_offset_);
}


off_t
LineReader::_NextLineOffset(off_t whence) 
{
  if (whence == LastOffset())
  {
    return -1;
  }

  off_t ret= whence;
  while	 (ret < buffer_size_ && buffer_[ret] != '\n') 
  {
    ret++;
  }	

  if (buffer_[ret] == '\n') ret++; // skip trailing newline
  return ret;
}


void
LineReader::CurrentLine(String *into) 
{
  return LineAt(current_offset_, into);
}


void
LineReader::LineAt(off_t offset, String *into) 
{
  off_t begin= offset; 
  off_t end= _NextLineOffset(offset);
  into->copy((const char *) buffer_ + begin, end - begin - 1, default_charset_info);
}


off_t
LineReader::CurrentOffset() 
{
  return current_offset_;
}


off_t
LineReader::LastOffset() 
{
  return buffer_size_;
}
