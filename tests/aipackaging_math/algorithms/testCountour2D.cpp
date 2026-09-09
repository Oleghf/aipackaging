#include <memory>
#include <utility>
#include <vector>

#include <boundedcurve2d.h>
#include <circle2d.h>
#include <countour2d.h>
#include <gtest/gtest.h>
#include <line2d.h>
#include <mathutils.h>
#include <point2d.h>

namespace
{
std::unique_ptr<BoundedCurve2D> makeLine(const Point2D & start, const Point2D & end)
{
  return std::make_unique<BoundedCurve2D>(std::make_shared<Line2D>(start, end - start), 0.0, distance(start, end));
}

std::unique_ptr<BoundedCurve2D> makeArc(const Point2D & center, double radius, double minT, double maxT)
{
  return std::make_unique<BoundedCurve2D>(std::make_shared<Circle2D>(center, radius), minT, maxT);
}

std::vector<std::unique_ptr<BoundedCurve2D>> makeSquare()
{
  const Point2D bottomLeft{0.0, 0.0};
  const Point2D bottomRight{1.0, 0.0};
  const Point2D topRight{1.0, 1.0};
  const Point2D topLeft{0.0, 1.0};

  std::vector<std::unique_ptr<BoundedCurve2D>> segments;
  segments.push_back(makeLine(bottomLeft, bottomRight));
  segments.push_back(makeLine(bottomRight, topRight));
  segments.push_back(makeLine(topRight, topLeft));
  segments.push_back(makeLine(topLeft, bottomLeft));
  return segments;
}
} // namespace

TEST(Countour2DCreate, RejectsEmptySegments)
{
  EXPECT_EQ(Countour2D::Create({}), nullptr);
}

TEST(Countour2DCreate, RejectsNullSegment)
{
  std::vector<std::unique_ptr<BoundedCurve2D>> segments;
  segments.push_back(nullptr);

  EXPECT_EQ(Countour2D::Create(std::move(segments)), nullptr);
}

TEST(Countour2DCreate, RejectsDisconnectedSegments)
{
  std::vector<std::unique_ptr<BoundedCurve2D>> segments;
  segments.push_back(makeLine({0.0, 0.0}, {1.0, 0.0}));
  segments.push_back(makeLine({2.0, 0.0}, {0.0, 0.0}));

  EXPECT_EQ(Countour2D::Create(std::move(segments)), nullptr);
}

TEST(Countour2DCreate, RejectsDegenerateSegment)
{
  std::vector<std::unique_ptr<BoundedCurve2D>> segments;
  segments.push_back(makeArc({0.0, 0.0}, 0.0, 0.0, 2.0 * MathConstants::PI));

  EXPECT_EQ(Countour2D::Create(std::move(segments)), nullptr);
}

TEST(Countour2DCreate, AcceptsClosedSegmentsWithinTolerance)
{
  const double delta = MathConstants::TOLERANCE_DOUBLE / 2.0;
  std::vector<std::unique_ptr<BoundedCurve2D>> segments;
  segments.push_back(makeLine({0.0, 0.0}, {1.0, 0.0}));
  segments.push_back(makeLine({1.0 + delta, 0.0}, {1.0, 1.0}));
  segments.push_back(makeLine({1.0, 1.0}, {0.0, 1.0}));
  segments.push_back(makeLine({0.0, 1.0}, {0.0, delta}));

  EXPECT_NE(Countour2D::Create(std::move(segments)), nullptr);
}

TEST(Countour2DGetPoints, ReturnsSquareVertices)
{
  const std::unique_ptr<Countour2D> contour = Countour2D::Create(makeSquare());

  ASSERT_NE(contour, nullptr);
  const std::vector<Point2D> points = contour->GetPoints();
  ASSERT_EQ(points.size(), 4);
  EXPECT_EQ(points[0], (Point2D{0.0, 0.0}));
  EXPECT_EQ(points[1], (Point2D{1.0, 0.0}));
  EXPECT_EQ(points[2], (Point2D{1.0, 1.0}));
  EXPECT_EQ(points[3], (Point2D{0.0, 1.0}));
}

TEST(Countour2DGetPoints, SupportsSingleClosedCurve)
{
  std::vector<std::unique_ptr<BoundedCurve2D>> segments;
  segments.push_back(makeArc({0.0, 0.0}, 1.0, 0.0, 2.0 * MathConstants::PI));
  const std::unique_ptr<Countour2D> contour = Countour2D::Create(std::move(segments));

  ASSERT_NE(contour, nullptr);
  const std::vector<Point2D> points = contour->GetPoints();
  EXPECT_GT(points.size(), 16);
  EXPECT_NE(points.front(), points.back());
}

TEST(Countour2DGetPoints, SupportsArcClosedByLine)
{
  std::vector<std::unique_ptr<BoundedCurve2D>> segments;
  segments.push_back(makeArc({0.0, 0.0}, 1.0, 0.0, MathConstants::PI));
  segments.push_back(makeLine({-1.0, 0.0}, {1.0, 0.0}));
  const std::unique_ptr<Countour2D> contour = Countour2D::Create(std::move(segments));

  ASSERT_NE(contour, nullptr);
  EXPECT_GT(contour->GetPoints().size(), 16);
}

TEST(Countour2DGetPoints, IsRepeatable)
{
  const std::unique_ptr<Countour2D> contour = Countour2D::Create(makeSquare());

  ASSERT_NE(contour, nullptr);
  EXPECT_EQ(contour->GetPoints(), contour->GetPoints());
}
