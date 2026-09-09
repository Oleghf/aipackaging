#ifndef GENERATORID_H__
#define GENERATORID_H__

inline size_t generateID()
{
  static size_t prevGenerateID = -1;
  return ++prevGenerateID;
}

#endif
