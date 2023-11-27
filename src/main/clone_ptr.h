#include <memory>


template<typename T>
struct default_clone {
    T *operator()(const T *other) const {
        return other?other->clone():nullptr;
    }
};

template<typename T, typename Deleter = std::default_delete<T> , typename Cloner = default_clone<T> >
class clone_ptr: public std::unique_ptr<T, Deleter>  {
public:

    using std::unique_ptr<T, Deleter>::unique_ptr;
    clone_ptr(const clone_ptr &other)
        :std::unique_ptr<T, Deleter>(_cln(other.get()))
        ,_cln(other._cln) {}
    clone_ptr(std::unique_ptr<T,Deleter> &&other, Cloner &&cloner = Cloner())
        :std::unique_ptr<T,Deleter>(std::move(other))
        ,_cln(std::move(cloner)) {}
    clone_ptr(clone_ptr &&) = default;
    clone_ptr &operator=(const clone_ptr &other) {
        if (this != &other) {
            std::unique_ptr<T, Deleter>::operator =(std::unique_ptr<T, Deleter>(_cln(other.get())));
            _cln = other._cln;
        }
        return *this;
    }

protected:
    [[no_unique_address]] Cloner _cln;
};
