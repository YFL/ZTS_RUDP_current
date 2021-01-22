#ifndef _BYTE_ARRAY_H_
#define _BYTE_ARRAY_H_

#include <string>

#include <cstdint>

/**
 * @brief An array of bytes suitable to contain data to be sent or received from a network
 * It contains even a constructor and a to_string method to make it a convenient container
 * for c and c++ strings.
 */
class ByteArray
{
public:
    /**
     * @brief create a <size> sized ByteArray
     * @param size : uin64_t - the size of the underlying byte array
     */
    ByteArray(uint64_t size);
    /**
     * @brief create a ByteArray containg a copy of <str>
     * @param str : const std::string & - the string which we create a copy from
     * @param contain_trailing_zero : bool - whether we wish to contain the trailing zero from the string or not
     */
    ByteArray(const std::string &str, bool trailing_zero = true);
    /**
     * @brief create a ByteArray containing a copy of <array> of size <size>
     * @param array : const uin8_t * - the array which we create a copy from
     * @param size : uint64_t - the size of the array in bytes
     */
    ByteArray(const uint8_t *array, uint64_t size);

    /**
     * @brief copy constructor
     * @param copy : const ByteArray & - the ByteArray which we create a copy from by copying
     */
    ByteArray(const ByteArray &copy);
    /**
     * @brief move contructor (we keep the underlying array, no reallocation)
     * @param move : ByteArray && - the ByteArray which we create a copy from by moving
     */
    ByteArray(ByteArray &&move);
    /**
     * @brief destructor deletes the underlying array
     */
    ~ByteArray() noexcept;

public:
    /**
     * @brief copy assignment operator (we delete the previous array and allocate a new one)
     */
    auto operator=(const ByteArray &copy) -> ByteArray &;
    /**
     * @brief move assignment operator (we delete the previous array and allocate a new one)
     */
    auto operator=(ByteArray &&move) -> ByteArray &;
    /**
     * @brief concatenate another ByteArray to this trailing zeroes are not merged
     * If two strings with trailing zeores are concatenated both zeroes reamin in place
     * @param plus : const ByteArray & - the ByteArray which we create a copy from
     * @return ByteArray - a new ByteArray containing the contents of this and the other's after this'
     */
    auto operator+(const ByteArray &plus) const -> ByteArray;
    /**
     * @brief concatenate a byte to this
     * @param byte : const uint8_t - the byte we are to append to this
     * @return ByteArray - a new ByteArray containing the contents of this plus the byte appended at the end
     */
    auto operator+(const uint8_t byte) const -> ByteArray;
    /**
     * @brief same as the concatenateion only it mutates this
     */
    auto operator+=(const ByteArray &plus) -> ByteArray &;
    /**
     * @brief same as the concatenation inly it mutates this
     */
    auto operator+=(const uint8_t byte) -> ByteArray &;
    /**
     * @brief array element access operator
     * @return a reference to the element at the position i.
     * @throw std::out_of_bounds if index >= this->size() || index < 0
     */
    auto operator[](uint64_t index) -> uint8_t &;
    /**
     * @brief same as array element access operator only it returns a copy of the byte at index
     */
    auto operator[](uint64_t index) const -> const uint8_t &;
    /**
     * @brief equality comparison operator
     * Checks for equelity in size and contents
     */
    auto operator==(const ByteArray &other) const -> bool;
    /**
     * @brief inequality operator
     * Checks for inequality in size and contents
     */
    auto operator!=(const ByteArray &other) const -> bool;

public:
    /**
     * @brief create a string representation of the underlying array
     * The bytes are treated as bytes not as charactes of a string
     * @return std::string - The output is a string containing multiline data of ints separated by spaces
     * and new lines
     */
    auto to_string() const -> std::string;

public:
    /**
     * @brief return the number of bytes in this array
     */
    auto size() const -> uint64_t;
    /**
     * @brief return the raw underlying pointer
     * WARNING it's not safe to modify it, or make it point elsewhere
     * hence the consts in the 
     */
    auto get() -> uint8_t const *const;

private:
    uint8_t *_array;
    uint64_t _size;
};

#endif // _BYTE_ARRAY_H_