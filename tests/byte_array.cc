#include "byte_array.h"

#include <utility>
#include <stdexcept>

#include <string.h>

ByteArray::ByteArray(uint64_t size) :
    _array(nullptr),
    _size(size)
{
    if(!size)
    {
        return;
    }
    _array = new uint8_t[_size];
}

ByteArray::ByteArray(const std::string &str, bool trailing_zero) :
    _array(nullptr),
    _size(trailing_zero ? str.size() + 1 : str.size())
{
    _array = new uint8_t[_size];
    memcpy(_array, str.c_str(), _size);
}

ByteArray::ByteArray(const uint8_t *ar, uint64_t size) :
    _array(nullptr),
    _size(size)
{
    _array = new uint8_t[_size];
    for(uint64_t i = 0; i < _size; i++)
    {
        _array[i] = ar[i];
    }
}

ByteArray::ByteArray(const ByteArray &copy) :
    _array(nullptr)
{
    *this = copy;
}

ByteArray::ByteArray(ByteArray &&move) :
    _array(nullptr)
{
    *this = std::move(move);
}

ByteArray::~ByteArray() noexcept
{
    delete[] _array;
}

auto ByteArray::operator=(const ByteArray &copy) -> ByteArray &
{
    delete[] _array;
    _array = new uint8_t[copy._size];
    _size = copy._size;
    memcpy(_array, copy._array, _size);

    return *this;
}

auto ByteArray::operator=(ByteArray &&move) -> ByteArray &
{
    std::swap(_array, move._array);
    _size = move._size;

    return *this;
}

auto ByteArray::operator+(const ByteArray &plus) const -> ByteArray
{
    ByteArray new_array(_size + plus._size);
    for(uint64_t i = 0; i < _size + plus._size; i++)
    {
        new_array[i] = i < _size ? _array[i] : plus._array[i - _size];
    }

    return new_array;
}

auto ByteArray::operator+(const uint8_t byte) const -> ByteArray
{
    ByteArray new_array(_size + 1);
    for(uint64_t i = 0; i < _size; i++)
    {
        new_array[i] = _array[i];
    }
    new_array[_size] = byte;

    return new_array;
}

auto ByteArray::operator+=(const ByteArray &plus) -> ByteArray &
{
    *this = *this + plus;

    return *this;
}

auto ByteArray::operator+=(const uint8_t byte) -> ByteArray &
{
    *this = *this + byte;

    return *this;
}

auto ByteArray::operator[](uint64_t index) -> uint8_t &
{
    if(index < _size && index >= 0)
    {
        return _array[index];
    }

    throw std::out_of_range("index out of bounds");
}

auto ByteArray::operator[](uint64_t index) const -> const uint8_t &
{
    return (*this)[index];
}

auto ByteArray::operator==(const ByteArray &other) const -> bool
{
    if(_size != other._size)
    {
        return false;
    }

    for(uint64_t i = 0; i < _size; i++)
    {
        if(_array[i] != other._array[i])
        {
            return false;
        }
    }

    return true;
}

auto ByteArray::operator!=(const ByteArray &other) const -> bool
{
    return !(*this == other);
}

auto ByteArray::to_string() const -> std::string
{
    std::string s;
    for(uint64_t i = 0; i < _size; i++)
    {
        s += std::to_string(static_cast<int>(_array[i]));
        if(i % 16 == 15)
        {
            s += "\n";
            continue;
        }
        else if(i % 8 == 7)
        {
            s += "  ";
        }
        s += " ";
    }
    if(s.back() != '\n')
    {
        s += "\n";
    }

    return s;
}

auto ByteArray::size() const -> uint64_t
{
    return _size;
}

auto ByteArray::get() -> uint8_t const *const
{
    return _array;
}