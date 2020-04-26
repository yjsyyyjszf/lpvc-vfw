#ifndef LPVC_VFW_DIB_ITERATOR_H
#define LPVC_VFW_DIB_ITERATOR_H

#include <lpvc/lpvc.h>
#include <cmath>
#include <iterator>


// ===========================================================================
//  DIBIteratorImpl
// ===========================================================================

template<typename BitmapType, typename ValueType>
class DIBIteratorImpl final
{
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = ValueType;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;

  DIBIteratorImpl(long bitmapWidth, long bitmapHeight, BitmapType* bitmap) noexcept :
    bitmapWidth_(bitmapWidth),
    bitmap_(bitmap)
  {
    if(bitmapHeight > 0)
    {
      positionY_ = std::abs(bitmapHeight) - 1;
      stepY_ = -1;
    }
  }

  DIBIteratorImpl& operator++() noexcept
  {
    if(++positionX_ == bitmapWidth_)
    {
      positionX_ = 0;
      positionY_ += stepY_;
    }

    return *this;
  }

  value_type operator*() const noexcept
  {
    return { &bitmap_[positionX_ + positionY_ * bitmapWidth_] };
  }

private:
  long bitmapWidth_ = 0;
  BitmapType* bitmap_ = nullptr;

  long positionX_ = 0;
  long positionY_ = 0;
  long stepY_ = 1;
};


// ===========================================================================
//  DIBIterator
// ===========================================================================

namespace detail
{

struct SwappedColor final
{
  SwappedColor& operator=(const lpvc::Color& color) noexcept
  {
    *color_ = { color.b, color.g, color.r };

    return *this;
  }

  lpvc::Color* color_ = nullptr;
};

} // namespace detail


using DIBIterator = DIBIteratorImpl<lpvc::Color, detail::SwappedColor>;


// ===========================================================================
//  DIBConstIterator
// ===========================================================================

namespace detail
{

struct SwappedColorConst final
{
  operator lpvc::Color() const noexcept
  {
    return { color_->b, color_->g, color_->r };
  }

  const lpvc::Color* color_ = nullptr;
};

} // namespace detail


using DIBConstIterator = DIBIteratorImpl<const lpvc::Color, detail::SwappedColorConst>;


#endif // LPVC_VFW_DIB_ITERATOR_H
