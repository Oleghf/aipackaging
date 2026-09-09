#ifndef APPLICATIONSTATESERVICE_H__
#define APPLICATIONSTATESERVICE_H__

#include <memory>

class IStatisticsView;
class IState;
class PackingController;

namespace ApplicationStateService
{
class IStateSession
{
public:
  virtual ~IStateSession() = default;
  virtual std::shared_ptr<IState> current() const = 0;
  virtual void toggle() = 0;
};

std::shared_ptr<IStateSession> create(std::shared_ptr<PackingController> packingController);
} // namespace ApplicationStateService

#endif
