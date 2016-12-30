
//          Copyright Oliver Kowalke 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_CONTEXT_V1_CONTINUATION_H
#define BOOST_CONTEXT_V1_CONTINUATION_H

#include <boost/context/detail/config.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <ostream>
#include <tuple>
#include <utility>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/intrusive_ptr.hpp>

#if defined(BOOST_NO_CXX17_STD_APPLY)
#include <boost/context/detail/apply.hpp>
#endif
#if defined(BOOST_NO_CXX17_STD_INVOKE)
#include <boost/context/detail/invoke.hpp>
#endif
#include <boost/context/detail/disable_overload.hpp>
#include <boost/context/detail/exception.hpp>
#include <boost/context/detail/exchange.hpp>
#include <boost/context/detail/fcontext.hpp>
#include <boost/context/detail/tuple.hpp>
#include <boost/context/fixedsize_stack.hpp>
#include <boost/context/flags.hpp>
#include <boost/context/preallocated.hpp>
#include <boost/context/segmented_stack.hpp>
#include <boost/context/stack_context.hpp>

#ifdef BOOST_HAS_ABI_HEADERS
# include BOOST_ABI_PREFIX
#endif

#if defined(BOOST_MSVC)
# pragma warning(push)
# pragma warning(disable: 4702)
#endif

namespace boost {
namespace context {
namespace detail {
inline namespace v1 {

template< int N >
struct helper {
    template< typename T >
    static T convert( T && t) noexcept {
        return std::forward< T >( t);
    }
};

template<>
struct helper< 1 > {
    template< typename T >
    static std::tuple< T > convert( T && t) noexcept {
        return std::make_tuple( std::forward< T >( t) );
    }
};

inline
transfer_t context_unwind( transfer_t t) {
    throw forced_unwind( t.fctx);
    return { nullptr, nullptr };
}

template< typename Rec >
transfer_t context_exit( transfer_t t) noexcept {
    Rec * rec = static_cast< Rec * >( t.data);
    // destroy context stack
    rec->deallocate();
    return { nullptr, nullptr };
}

template< typename Rec >
void context_entry( transfer_t t_) noexcept {
    // transfer control structure to the context-stack
    Rec * rec = static_cast< Rec * >( t_.data);
    BOOST_ASSERT( nullptr != rec);
    transfer_t t = { nullptr, nullptr };
    try {
        // jump back to `context_create()`
        t = jump_fcontext( t_.fctx, nullptr);
        // start executing
        t = rec->run( t);
    } catch ( forced_unwind const& e) {
        t = { e.fctx, nullptr };
    }
    BOOST_ASSERT( nullptr != t.fctx);
    // destroy context-stack of `this`context on next context
    ontop_fcontext( t.fctx, rec, context_exit< Rec >);
    BOOST_ASSERT_MSG( false, "context already terminated");
}

template<
    typename Ctx,
    typename ArgTuple,
    typename StackAlloc,
    typename Fn
>
class record {
private:
    StackAlloc                                          salloc_;
    stack_context                                       sctx_;
    typename std::decay< Fn >::type                     fn_;

    static void destroy( record * p) noexcept {
        StackAlloc salloc = p->salloc_;
        stack_context sctx = p->sctx_;
        // deallocate record
        p->~record();
        // destroy stack with stack allocator
        salloc.deallocate( sctx);
    }

public:
    record( stack_context sctx, StackAlloc const& salloc,
            Fn && fn) noexcept :
        salloc_( salloc),
        sctx_( sctx),
        fn_( std::forward< Fn >( fn) ) {
    }

    record( record const&) = delete;
    record & operator=( record const&) = delete;

    void deallocate() noexcept {
        destroy( this);
    }

    transfer_t run( transfer_t t) {
        Ctx from{ t.fctx };
        ArgTuple args = std::move( * static_cast< ArgTuple * >( t.data) );
        auto tpl = std::tuple_cat(
                std::forward_as_tuple( std::move( from) ),
                std::move( args) );
        // invoke context-function
#if defined(BOOST_NO_CXX17_STD_APPLY)
        Ctx cc = apply( std::move( fn_), std::move( tpl) );
#else
        Ctx cc = std::apply( std::move( fn_), std::move( tpl) );
#endif
        return { exchange( cc.fctx_, nullptr), nullptr };
    }
};

template<
    typename Ctx,
    typename StackAlloc,
    typename Fn
>
class record_void {
private:
    StackAlloc                                          salloc_;
    stack_context                                       sctx_;
    typename std::decay< Fn >::type                     fn_;

    static void destroy( record_void * p) noexcept {
        StackAlloc salloc = p->salloc_;
        stack_context sctx = p->sctx_;
        // deallocate record
        p->~record_void();
        // destroy stack with stack allocator
        salloc.deallocate( sctx);
    }

public:
    record_void( stack_context sctx,
                 StackAlloc const& salloc,
                 Fn && fn) noexcept :
        salloc_{ salloc },
        sctx_{ sctx },
        fn_{ std::forward< Fn >( fn) } {
    }

    record_void( record_void const&) = delete;
    record_void & operator=( record_void const&) = delete;

    void deallocate() noexcept {
        destroy( this);
    }

    transfer_t run( transfer_t t) {
        Ctx from{ t.fctx };
        // invoke context-function
        Ctx cc = fn_( std::move( from) );
        return { exchange( cc.fctx_, nullptr), nullptr };
    }
};

template< typename Record, typename StackAlloc, typename Fn >
fcontext_t context_create( StackAlloc salloc, Fn && fn) {
    auto sctx = salloc.allocate();
    // reserve space for control structure
#if defined(BOOST_NO_CXX11_CONSTEXPR) || defined(BOOST_NO_CXX11_STD_ALIGN)
    const std::size_t size = sctx.size - sizeof( Record);
    void * sp = static_cast< char * >( sctx.sp) - sizeof( Record);
#else
    constexpr std::size_t func_alignment = 64; // alignof( Record);
    constexpr std::size_t func_size = sizeof( Record);
    // reserve space on stack
    void * sp = static_cast< char * >( sctx.sp) - func_size - func_alignment;
    // align sp pointer
    std::size_t space = func_size + func_alignment;
    sp = std::align( func_alignment, func_size, sp, space);
    BOOST_ASSERT( nullptr != sp);
    // calculate remaining size
    const std::size_t size = sctx.size - ( static_cast< char * >( sctx.sp) - static_cast< char * >( sp) );
#endif
    // create fast-context
    const fcontext_t fctx = make_fcontext( sp, size, & context_entry< Record >);
    BOOST_ASSERT( nullptr != fctx);
    // placment new for control structure on context-stack
    auto rec = ::new ( sp) Record{
            sctx, salloc, std::forward< Fn >( fn) };
    // transfer control structure to context-stack
    return jump_fcontext( fctx, rec).fctx;
}

template< typename Record, typename StackAlloc, typename Fn >
fcontext_t context_create( preallocated palloc, StackAlloc salloc, Fn && fn) {
    // reserve space for control structure
#if defined(BOOST_NO_CXX11_CONSTEXPR) || defined(BOOST_NO_CXX11_STD_ALIGN)
    const std::size_t size = palloc.size - sizeof( Record);
    void * sp = static_cast< char * >( palloc.sp) - sizeof( Record);
#else
    constexpr std::size_t func_alignment = 64; // alignof( Record);
    constexpr std::size_t func_size = sizeof( Record);
    // reserve space on stack
    void * sp = static_cast< char * >( palloc.sp) - func_size - func_alignment;
    // align sp pointer
    std::size_t space = func_size + func_alignment;
    sp = std::align( func_alignment, func_size, sp, space);
    BOOST_ASSERT( nullptr != sp);
    // calculate remaining size
    const std::size_t size = palloc.size - ( static_cast< char * >( palloc.sp) - static_cast< char * >( sp) );
#endif
    // create fast-context
    const fcontext_t fctx = make_fcontext( sp, size, & context_entry< Record >);
    BOOST_ASSERT( nullptr != fctx);
    // placment new for control structure on context-stack
    auto rec = ::new ( sp) Record{
            palloc.sctx, salloc, std::forward< Fn >( fn) };
    // transfer control structure to context-stack
    return jump_fcontext( fctx, rec).fctx;
}

template<
    typename Ctx,
    typename ... Arg
>
struct result_type {
    typedef std::tuple< Ctx, Arg ... >   type;

    static
    type get( detail::transfer_t t) {
        BOOST_ASSERT( nullptr != t.data);
        auto p = static_cast< std::tuple< Arg ... > * >( t.data);
        return std::tuple_cat( std::forward_as_tuple( Ctx{ t.fctx } ), * p); 
    }
};

template< typename Ctx >
struct result_type< Ctx, void > {
    typedef Ctx     type;

    static
    type get( detail::transfer_t t) {
        BOOST_ASSERT( nullptr == t.data);
        return Ctx{ t.fctx };
    }
};

}}

inline namespace v1 {

class continuation {
private:
    template< typename Ctx, typename ArpTuple, typename StackAlloc, typename Fn >
    friend class detail::record;

    template< typename Ctx, typename StackAlloc, typename Fn >
    friend class detail::record_void;

    template< typename Ctx, typename ... Arg >
    friend struct detail::result_type;

    template< typename Ctx, typename Fn, typename ... Arg >
    friend detail::transfer_t
    context_ontop( detail::transfer_t);

    template< typename Ctx, typename Fn >
    friend detail::transfer_t
    context_ontop_void( detail::transfer_t);

    template< typename ... Ret, typename StackAlloc, typename Fn, typename ... Arg >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( std::allocator_arg_t, StackAlloc, Fn &&, Arg ...);

    template< typename ... Ret, typename StackAlloc, typename Fn, typename ... Arg >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( std::allocator_arg_t, preallocated, StackAlloc, Fn &&, Arg ...);

    template< typename ... Ret, typename ... Arg >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( continuation &&, Arg ...);

    template< typename ... Ret, typename Fn, typename ... Arg >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( continuation &&, exec_ontop_arg_t, Fn &&, Arg ...);

    template< typename ... Ret, typename StackAlloc, typename Fn >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( std::allocator_arg_t, StackAlloc, Fn &&);

    template< typename ... Ret, typename StackAlloc, typename Fn >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( std::allocator_arg_t, preallocated, StackAlloc, Fn &&);

    template< typename ... Ret >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( continuation &&);

    template< typename ... Ret, typename Fn >
    friend typename detail::result_type< continuation, Ret ... >::type
    callcc( continuation &&, exec_ontop_arg_t, Fn &&);

    detail::fcontext_t  fctx_{ nullptr };

    continuation( detail::fcontext_t fctx) noexcept :
        fctx_{ fctx } {
    }

public:
    continuation() noexcept = default;

    ~continuation() {
        if ( nullptr != fctx_) {
            detail::ontop_fcontext( detail::exchange( fctx_, nullptr), nullptr, detail::context_unwind);
        }
    }

    continuation( continuation && other) noexcept :
        fctx_{ other.fctx_ } {
        other.fctx_ = nullptr;
    }

    continuation & operator=( continuation && other) noexcept {
        if ( this != & other) {
            continuation tmp = std::move( other);
            swap( tmp);
        }
        return * this;
    }

    continuation( continuation const& other) noexcept = delete;
    continuation & operator=( continuation const& other) noexcept = delete;

    explicit operator bool() const noexcept {
        return nullptr != fctx_;
    }

    bool operator!() const noexcept {
        return nullptr == fctx_;
    }

    bool operator==( continuation const& other) const noexcept {
        return fctx_ == other.fctx_;
    }

    bool operator!=( continuation const& other) const noexcept {
        return fctx_ != other.fctx_;
    }

    bool operator<( continuation const& other) const noexcept {
        return fctx_ < other.fctx_;
    }

    bool operator>( continuation const& other) const noexcept {
        return other.fctx_ < fctx_;
    }

    bool operator<=( continuation const& other) const noexcept {
        return ! ( * this > other);
    }

    bool operator>=( continuation const& other) const noexcept {
        return ! ( * this < other);
    }

    template< typename charT, class traitsT >
    friend std::basic_ostream< charT, traitsT > &
    operator<<( std::basic_ostream< charT, traitsT > & os, continuation const& other) {
        if ( nullptr != other.fctx_) {
            return os << other.fctx_;
        } else {
            return os << "{not-a-context}";
        }
    }

    void swap( continuation & other) noexcept {
        std::swap( fctx_, other.fctx_);
    }
};

template< typename Ctx, typename Fn, typename ... Arg >
detail::transfer_t context_ontop( detail::transfer_t t) {
    auto tpl = static_cast< std::tuple< Fn, std::tuple< Arg ... > > * >( t.data);
    BOOST_ASSERT( nullptr != tpl);
    typename std::decay< Fn >::type fn = std::forward< Fn >( std::get< 0 >( * tpl) );
    Ctx c{ t.fctx };
    auto args = std::tuple_cat( std::ref( c), std::move( std::get< 1 >( tpl) ) );
    // execute function, pass continuation via reference
#if defined(BOOST_NO_CXX17_STD_APPLY)
    std::get< 1 >( * tpl) = detail::helper< sizeof ... (Arg) >::convert( apply( fn, std::move( args) ) );
#else
    std::get< 1 >( * tpl) = detail::helper< sizeof ... (Arg) >::convert( std::apply( fn, std::move( args) ) );
#endif
    BOOST_ASSERT( nullptr != c.fctx_);
    return { detail::exchange( c.fctx_, nullptr), & std::get< 1 >( * tpl) };
}

template< typename Ctx, typename Fn >
detail::transfer_t context_ontop_void( detail::transfer_t t) {
    auto tpl = static_cast< std::tuple< Fn > * >( t.data);
    BOOST_ASSERT( nullptr != tpl);
    typename std::decay< Fn >::type fn = std::forward< Fn >( std::get< 0 >( * tpl) );
    Ctx c{ t.fctx };
    // execute function, pass continuation via reference
    fn( c);
    BOOST_ASSERT( nullptr != c.fctx_);
    return { detail::exchange( c.fctx_, nullptr), nullptr };
}

// Arg
template<
    typename ... Ret,
    typename Fn,
    typename ... Arg,
    typename = detail::disable_overload< continuation, Fn >
>
typename detail::result_type< continuation, Ret ... >::type
callcc( Fn && fn, Arg ... arg) {
    return callcc< Ret ... >(
            std::allocator_arg, fixedsize_stack(),
            std::forward< Fn >( fn), std::forward< Arg >( arg) ...);
}

template<
    typename ... Ret,
    typename StackAlloc,
    typename Fn,
    typename ... Arg
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, StackAlloc salloc, Fn && fn, Arg ... arg) {
    using ArgTuple = std::tuple< Arg ... >;
    using Record = detail::record< continuation, ArgTuple, StackAlloc, Fn >;
    return callcc< Ret ... >(
            continuation{ detail::context_create< Record >( salloc, std::forward< Fn >( fn) ) },
            std::forward< Arg >( arg) ... );
}

template<
    typename ... Ret,
    typename StackAlloc,
    typename Fn,
    typename ... Arg
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, preallocated palloc, StackAlloc salloc, Fn && fn, Arg ... arg) {
    using ArgTuple = std::tuple< Arg ... >;
    using Record = detail::record< continuation, ArgTuple, StackAlloc, Fn >;
    return callcc< Ret ... >(
            continuation{ detail::context_create< Record >( palloc, salloc, std::forward< Fn >( fn) ) },
            std::forward< Arg >( arg) ... );
}

template<
    typename ... Ret,
    typename ... Arg
>
typename detail::result_type< continuation, Ret ... >::type
callcc( continuation && c, Arg ... arg) {
    BOOST_ASSERT( c);
    auto tpl{ std::forward< Arg >( arg) ... };
    return detail::result_type< continuation, Ret ... >::get(
            detail::jump_fcontext(
                detail::exchange( c.fctx_, nullptr),
                & tpl) );
}

template<
    typename ... Ret,
    typename Fn,
    typename ... Arg
>
typename detail::result_type< continuation, Ret ... >::type
callcc( continuation && c, exec_ontop_arg_t, Fn && fn, Arg ... arg) {
    BOOST_ASSERT( c);
    auto tpl = std::make_tuple( std::forward< Fn >( fn), std::forward< Arg >( arg) ... );
    return detail::result_type< continuation, Ret ... >::get(
        detail::ontop_fcontext(
                detail::exchange( c.fctx_, nullptr),
                & tpl,
                context_ontop< continuation, Fn, Arg ... >) );
}

// void
template<
    typename ... Ret,
    typename Fn,
    typename = detail::disable_overload< continuation, Fn >
>
typename detail::result_type< continuation, Ret ... >::type
callcc( Fn && fn) {
    return callcc< Ret ... >(
            std::allocator_arg, fixedsize_stack(),
            std::forward< Fn >( fn) );
}

template<
    typename ... Ret,
    typename StackAlloc,
    typename Fn
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, StackAlloc salloc, Fn && fn) {
    using Record = detail::record_void< continuation, StackAlloc, Fn >;
    return callcc< Ret ... >(
            continuation{ detail::context_create< Record >( salloc, std::forward< Fn >( fn) ) } );
}

template<
    typename ... Ret,
    typename StackAlloc,
    typename Fn
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, preallocated palloc, StackAlloc salloc, Fn && fn) {
    using Record = detail::record_void< continuation, StackAlloc, Fn >;
    return callcc< Ret ... >(
            continuation{ detail::context_create< Record >( palloc, salloc, std::forward< Fn >( fn) ) } );
}

template< typename ... Ret >
typename detail::result_type< continuation, Ret ... >::type
callcc( continuation && c) {
    BOOST_ASSERT( c);
    return detail::result_type< continuation, Ret ... >::get(
            detail::jump_fcontext(
                detail::exchange( c.fctx_, nullptr), nullptr) );
}

template<
    typename ... Ret,
    typename Fn
>
typename detail::result_type< continuation, Ret ... >::type
callcc( continuation && c, exec_ontop_arg_t, Fn && fn) {
    BOOST_ASSERT( c);
    auto tpl = std::make_tuple( std::forward< Fn >( fn) );
    return detail::result_type< continuation, Ret ... >::get(
        detail::ontop_fcontext(
                detail::exchange( c.fctx_, nullptr),
                & tpl,
                context_ontop_void< continuation, Fn >) );
}

#if defined(BOOST_USE_SEGMENTED_STACKS)
template<
    typename ... Ret,
    typename Fn,
    typename ... Arg
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, segmented_stack, Fn &&, Arg ...);

template<
    typename ... Ret,
    typename StackAlloc,
    typename Fn,
    typename ... Arg
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, preallocated, segmented_stack, Fn &&, Arg ...);

template<
    typename ... Ret,
    typename Fn
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, segmented_stack, Fn &&);

template<
    typename ... Ret,
    typename StackAlloc,
    typename Fn
>
typename detail::result_type< continuation, Ret ... >::type
callcc( std::allocator_arg_t, preallocated, segmented_stack, Fn &&);
#endif

// swap
void swap( continuation & l, continuation & r) noexcept {
    l.swap( r);
}

}}}

#if defined(BOOST_MSVC)
# pragma warning(pop)
#endif

#ifdef BOOST_HAS_ABI_HEADERS
# include BOOST_ABI_SUFFIX
#endif

#endif // BOOST_CONTEXT_V1_CONTINUATION_H
