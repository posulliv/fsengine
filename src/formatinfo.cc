
#include <my_global.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <my_sys.h>

#include <string.h>

#include "formatinfo.h"

FormatInfo::FormatInfo() 
  : 
    skip_lines_(0), 
    separator_(0), 
    whitespace_separator_(true), 
    merge_separators_(false),
    path_(0) 
{}


FormatInfo::~FormatInfo() 
{
  if (path_)
  {
    my_free(path_, MYF(0));
  }
}


int
FormatInfo::Parse(const char *url) 
{
  char *urlcopy= my_strdup(url, MYF(0));

  /* must start with file:/// */
  if (strncmp(urlcopy, "file:///", 8))
  {
    return 0;
  }

  char *token= NULL;
  char *saveptr= NULL;
  token = strtok_r(urlcopy, ";", &saveptr);
  if (! token)
  {
    return 0;
  }

  path_= my_strdup(token + 7, MYF(0));

  while ((token = strtok_r(NULL, ";", &saveptr)) != NULL) 
  {
    char *option= my_strdup(token, MYF(0));
    char *saveptr2= NULL;
    char *key= strtok_r(option, "=", &saveptr2);
    char *value= strtok_r(NULL, "=", &saveptr2);

    if (!strcmp(option, "separator")) 
    {
      separator_= *value;
      whitespace_separator_ = false;
    }
    else if (! strcmp(option, "whitespace_separator")) 
    {
      whitespace_separator_= true;
    }
    else if (!strcmp(option, "skip_lines")) 
    {
      skip_lines_= atoi(value);
    }
    else 
    {
      my_free(path_, MYF(0));
      my_free(urlcopy, MYF(0));
      my_free(option, MYF(0));
      path_= NULL;
      return 0;
    }
    my_free(option, MYF(0));
  }

  my_free(urlcopy, MYF(0));
  return 1;
}


bool 
FormatInfo::ShouldSkip(CHARSET_INFO *charset, const char c) const 
{
  if (whitespace_separator_)
  {
    return my_isspace(charset, c);
  }
  else if (c == separator_)
  {
    return true;
  }
  return false;
}
