
#ifndef STORAGE_FORMATINFO_H
#define STORAGE_FORMATINFO_H

#include <include/m_ctype.h>

class FormatInfo 
{
public:
  FormatInfo();
  ~FormatInfo();

  int Parse(const char *url);

  const char *Path() const 
  { 
    return path_; 
  }

  int SkipLines() const 
  { 
    return skip_lines_; 
  }

  char Separator() const 
  { 
    return separator_; 
  }

  bool WhitespaceSeparator() const 
  { 
    return whitespace_separator_; 
  }

  bool MergeSeparators() const 
  { 
    return merge_separators_; 
  }

  bool ShouldSkip(CHARSET_INFO *charset, const char c) const;

private:

  int skip_lines_;
  char separator_;
  bool whitespace_separator_;
  bool merge_separators_;
  char *path_;

};

#endif /* STORAGE_FORMATINFO_H */
