#include <concepts>
#include <cstring>
#include <tuple>
#include <stdlib.h>

#define read_comp_pkt(size, ptr, t) const_for<size>([&](auto i){std::get<i.value>(t) = read_var<std::tuple_element_t<i.value, decltype(t)>>::call(&ptr);});

template<int size, typename T>
struct array_with_size
{
    T array;
    int s;

    static constexpr int getsize()
    {
        return size;
    }
};

template <typename T>
T read_type(char *v)
{
    T a;

    std::memcpy(&a, v, sizeof(T));

    return a;
}

template <int size, typename T>
array_with_size<size, T> read_array(char *v)
{
    array_with_size<size, T> a = {0};
    a.array = (T)calloc(size, sizeof(std::remove_pointer_t<T>));

    for (int i = 0; i < size; i++)
    {
        std::memcpy(&a.array[i], v, sizeof(std::remove_pointer_t<T>));
        v += sizeof(std::remove_pointer_t<T>);
    }

    return a;
}

// Template struct for reading variables
template<typename T>
concept arithmetic = std::integral<T> or std::floating_point<T>;

template<typename T>
concept IsPointer = std::is_pointer<T>::value;

template<typename T>
struct read_var
{
    static T call(char** v) requires (arithmetic<T>)
    {
        T ret = read_type<T>(*v);
        *v += sizeof(T);
        return ret;
    }
    static T call(char** v) requires (IsPointer<decltype(T::array)>)
    {
        array_with_size<T::getsize(), decltype(T::array)> arr = read_array<T::getsize(), decltype(T::array)>(*v);
        *v += (sizeof(std::remove_pointer_t<decltype(T::array)>) * T::getsize());
        return arr;
    }
};


template <typename Integer, Integer ...I, typename F> constexpr void const_for_each(std::integer_sequence<Integer, I...>, F&& func)
{
    (func(std::integral_constant<Integer, I>{}), ...);
}

template <auto N, typename F> constexpr void const_for(F&& func)
{
    if constexpr (N > 0)
        const_for_each(std::make_integer_sequence<decltype(N), N>{}, std::forward<F>(func));
}