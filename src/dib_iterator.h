#ifndef LPVC_VFW_DIB_ITERATOR_H
#define LPVC_VFW_DIB_ITERATOR_H

#include <lpvc/lpvc.h>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <type_traits>


namespace detail
{


// DIB surface stride formula for uncompressed bitmaps (from MSDN)
static std::size_t dibStride(std::size_t bitmapWidth) noexcept
{
  constexpr std::size_t bitsPerPixel = 24;

  return ((((bitmapWidth * bitsPerPixel) + 31) & ~31) >> 3);
}


// ===========================================================================
//  SwappedColor
// ===========================================================================

template<typename ColorType>
struct SwappedColor final
{
  SwappedColor& operator=(const lpvc::Color& color) noexcept
  {
    *color_ = { color.b, color.g, color.r };

    return *this;
  }

  operator lpvc::Color() const noexcept
  {
    return { color_->b, color_->g, color_->r };
  }

  ColorType* color_ = nullptr;
};


// ===========================================================================
//  DIBIteratorImpl
// ===========================================================================

template<typename ColorType>
class DIBIteratorImpl final
{
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = SwappedColor<ColorType>;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;
  
  using DataType = std::conditional_t<std::is_const_v<ColorType>, const std::byte, std::byte>;

  DIBIteratorImpl(std::size_t bitmapWidth, std::size_t bitmapHeight, DataType* bitmap) noexcept :
    bitmapWidth_(bitmapWidth),
    lineStep_(0),
    bitmap_(bitmap)
  {
    auto lineSize = dibStride(bitmapWidth);

    if(bitmapHeight > 0)
    {
      lineStep_ = bitmapWidth * sizeof(lpvc::Color) + lineSize;
      bitmap_ += (bitmapHeight - 1) * lineSize;
    }
  }

  DIBIteratorImpl& operator++() noexcept
  {
    bitmap_ += sizeof(lpvc::Color);

    if(++positionX_ == bitmapWidth_)
    {
      positionX_ = 0;
      bitmap_ -= lineStep_;
    }

    return *this;
  }

  value_type operator*() const noexcept
  {
    return { reinterpret_cast<ColorType*>(bitmap_) };
  }

private:
  std::size_t bitmapWidth_ = 0;
  std::size_t lineStep_ = 0;
  std::size_t positionX_ = 0;
  DataType* bitmap_ = nullptr;
};


} // namespace detail


using DIBIterator = detail::DIBIteratorImpl<lpvc::Color>;
using DIBConstIterator = detail::DIBIteratorImpl<const lpvc::Color>;


#endif // LPVC_VFW_DIB_ITERATOR_H
