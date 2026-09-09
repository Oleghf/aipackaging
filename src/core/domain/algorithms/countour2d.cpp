#include <cmath>
#include <utility>

#include <countour2d.h>
#include <mathutils.h>
#include <vector2d.h>

namespace
{
constexpr size_t POINTS_PER_SEGMENT = 60;

bool pointsEqual(const Point2D & lhs, const Point2D & rhs)
{
  return std::abs(lhs.x - rhs.x) <= MathConstants::TOLERANCE_DOUBLE && std::abs(lhs.y - rhs.y) <= MathConstants::TOLERANCE_DOUBLE;
}
} // namespace

Countour2D::Countour2D(std::vector<std::unique_ptr<BoundedCurve2D>> segments)
  : segments_(std::move(segments))
{
}

std::unique_ptr<Countour2D> Countour2D::Create(std::vector<std::unique_ptr<BoundedCurve2D>> segments)
{
  if (segments.empty())
    return nullptr;

  for (const std::unique_ptr<BoundedCurve2D> & segment : segments)
  {
    if (!segment || IsDegenerate(*segment))
      return nullptr;
  }

  std::unique_ptr<Countour2D> contour(new Countour2D(std::move(segments)));
  if (!contour->IsClosed())
    return nullptr;

  return contour;
}

bool Countour2D::IsClosed() const
{
  if (segments_.empty())
    return false;

  for (size_t index = 0; index < segments_.size(); ++index)
  {
    const BoundedCurve2D & current = *segments_[index];
    const BoundedCurve2D & next = *segments_[(index + 1) % segments_.size()];
    const Point2D currentEnd = current.getPoint(current.getMaxT());
    const Point2D nextStart = next.getPoint(next.getMinT());

    if (!pointsEqual(currentEnd, nextStart))
      return false;
  }

  return true;
}

bool Countour2D::IsDegenerate(const BoundedCurve2D & segment)
{
  const double minT = segment.getMinT();
  const double maxT = segment.getMaxT();
  if (!std::isfinite(minT) || !std::isfinite(maxT) || std::abs(maxT - minT) <= MathConstants::TOLERANCE_DOUBLE)
    return true;

  const Point2D start = segment.getPoint(minT);
  const Point2D middle = segment.getPoint((minT + maxT) / 2.0);
  const Point2D end = segment.getPoint(maxT);
  return pointsEqual(start, middle) && pointsEqual(middle, end);
}

std::vector<Point2D> Countour2D::SampleSegment(const BoundedCurve2D & segment)
{
  std::vector<Point2D> samples;
  samples.reserve(POINTS_PER_SEGMENT);

  const double minT = segment.getMinT();
  const double step = (segment.getMaxT() - minT) / static_cast<double>(POINTS_PER_SEGMENT - 1);
  for (size_t index = 0; index < POINTS_PER_SEGMENT; ++index)
    samples.push_back(segment.getPoint(minT + step * static_cast<double>(index)));

  std::vector<Point2D> result;
  result.reserve(samples.size());
  result.push_back(samples.front());

  for (size_t index = 1; index + 1 < samples.size(); ++index)
  {
    const Vector2D previous(samples[index] - samples[index - 1]);
    const Vector2D next(samples[index + 1] - samples[index]);
    if (std::abs(previous.pseudoCrossProduct(next)) > MathConstants::TOLERANCE_DOUBLE)
      result.push_back(samples[index]);
  }

  if (!pointsEqual(result.back(), samples.back()))
    result.push_back(samples.back());

  return result;
}

std::vector<Point2D> Countour2D::GetPoints() const
{
  std::vector<Point2D> result;

  for (const std::unique_ptr<BoundedCurve2D> & segment : segments_)
  {
    const std::vector<Point2D> segmentPoints = SampleSegment(*segment);
    for (const Point2D & point : segmentPoints)
    {
      if (result.empty() || !pointsEqual(result.back(), point))
        result.push_back(point);
    }
  }

  if (result.size() > 1 && pointsEqual(result.front(), result.back()))
    result.pop_back();

  return result;
}
