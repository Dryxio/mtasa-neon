/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CPtrNodeSingleListSA.h
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <cstddef>

template <class T>
struct CPtrNodeSingleLink
{
    T*                  pItem;
    CPtrNodeSingleLink* pNext;
};

template <class T>
class CPtrNodeSingleListSAInterface
{
public:
    void   RemoveItem(T* item);
    void   RemoveAllItems();
    size_t CountItemOccurrences(const T* item) const;

private:
    CPtrNodeSingleLink<T>* m_pList;
};

template <class T>
void CPtrNodeSingleListSAInterface<T>::RemoveItem(T* item)
{
    using CPtrNodeSingleList_RemoveItem_t = void(__thiscall*)(CPtrNodeSingleListSAInterface<T> * pLinkList, void* item);
    ((CPtrNodeSingleList_RemoveItem_t)0x533610)(this, item);
}

template <class T>
void CPtrNodeSingleListSAInterface<T>::RemoveAllItems()
{
    while (m_pList)
    {
        RemoveItem(m_pList->pItem);
    }
}

template <class T>
size_t CPtrNodeSingleListSAInterface<T>::CountItemOccurrences(const T* item) const
{
    size_t occurrences = 0;
    for (const CPtrNodeSingleLink<T>* node = m_pList; node; node = node->pNext)
    {
        if (node->pItem == item)
            ++occurrences;
    }
    return occurrences;
}
