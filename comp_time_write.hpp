#include <concepts>
#include <cstring>
#include <tuple>

#define write_comp_pkt(size, ptr, t) const_for<size>([&](auto i){write_var<std::tuple_element_t<i.value, decltype(t)>>::call(&ptr, std::get<i.value>(t));});

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
void write_type(char *v, T value)
{
    std::memcpy(v, &value, sizeof(T));
}

template <int size, typename T>
void write_array(char *v, T value)
{
    std::memcpy(v, value, size);
}

template <typename T>
void write_string(char *v, T value)
{
    std::strcpy(v, value);
}


template<typename T>
concept arithmetic = std::integral<T> or std::floating_point<T>;

template<typename T>
concept IsChar = std::same_as<T, char *>;

template<typename T>
concept IsPointer = std::is_pointer_v<T>;

template<typename T>
struct write_var
{
    static void call(char** v, T value) requires (arithmetic<T>)
    {
        write_type<T>(*v, value);
        *v += sizeof(T);
    }
    static void call(char** v, T value) requires (IsChar<decltype(T::array)>)
    {
        write_string<char *>(*v, value.array);
        *v += (sizeof(std::remove_pointer_t<decltype(T::array)>) * T::getsize());
    }
    static void call(char** v, T value) requires(!IsChar<decltype(T::array)> && IsPointer<decltype(T::array)>)
    {
        write_array<T::getsize(), decltype(T::array)>(*v, value.array);
        *v += (sizeof(std::remove_pointer_t<decltype(T::array)>) * T::getsize());
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