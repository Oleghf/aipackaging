#ifndef CONTOUR2D_H
#define CONTOUR2D_H

#include <memory>
#include <vector>

#include <boundedcurve2d.h>
#include <point2d.h>

class Countour2D
{
public:
  static std::unique_ptr<Countour2D> Create(std::vector<std::unique_ptr<BoundedCurve2D>> segments);

  std::vector<Point2D> GetPoints() const;

private:
  explicit Countour2D(std::vector<std::unique_ptr<BoundedCurve2D>> segments);

  bool IsClosed() const;
  static bool IsDegenerate(const BoundedCurve2D & segment);
  static std::vector<Point2D> SampleSegment(const BoundedCurve2D & segment);

  std::vector<std::unique_ptr<BoundedCurve2D>> segments_;
};

#endif
