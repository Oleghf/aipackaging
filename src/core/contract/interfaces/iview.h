#ifndef IVIEW_H__
#define IVIEW_H__

#include <memory>

#include <eventlistener.h>
#include <rect2d.h>


// Р”РµР№СЃС‚РІРёСЏ СЂРµРґР°РєС‚РѕСЂР°
enum class PackagingAction
{
  ChangeMode,
  OpenScene,
  Load,
  Save,
  SaveAs,
  Undo,
  Redo
};


// РўРёРї СЃРѕРѕР±С‰РµРЅРёСЏ
enum class MessageType
{
  Info,
  Warning,
  Error
};


////////////////////////////////////////////////////////////////////////////////
//
/// РРЅС‚РµСЂС„РµР№СЃ РґР»СЏ РІР·Р°РёРјРѕРґРµР№СЃС‚РІРёСЏ СЃ РїСЂРµРґСЃС‚Р°РІР»РµРЅРёРµРј
/**
*/
////////////////////////////////////////////////////////////////////////////////
class IStatisticsView
{
public:
  virtual void statisticChangeCountAllCells(unsigned int allCells) = 0;
  virtual void statisticChangeCountOccupiedCells(unsigned int occupiedCells) = 0;
};


class IFileDialogView
{
public:
  virtual std::string openSaveFileDialog(const std::string & title, const std::string & initPath,
                                         const std::string & filter = {}) = 0;
  virtual std::string openLoadFileDialog(const std::string & title, const std::string & initPath,
                                         const std::string & filter = {}) = 0;
};


class IRedrawView
{
public:
  virtual void requestRedraw() = 0;
};


class IActionView
{
public:
  // Р’РєР»СЋС‡РёС‚СЊ/РћС‚РєР»СЋС‡РёС‚СЊ РґРµР№СЃС‚РІРёРµ
  virtual void setActionEnabled(PackagingAction action, bool isEnabled) = 0;
  virtual bool isActionEnabled(PackagingAction action) const = 0;
};


class IView : public IStatisticsView,
              public IFileDialogView,
              public IRedrawView,
              public IActionView
{
public:
  // Р”РёР°Р»РѕРіРё
  virtual void showMessage(const std::string & title, const std::string & message, MessageType type) = 0;

  // Р”РѕР±Р°РІРёС‚СЊ/РЈРґР°Р»РёС‚СЊ СЃР»СѓС€Р°С‚РµР»РµР№ РёРІРµРЅС‚РѕРІ
  virtual void addEventListener(std::shared_ptr<EventListener> listener) = 0;
  virtual void removeEventListener(std::shared_ptr<EventListener> listener) = 0;

  // Р—СѓРј
  virtual void setZoomFactor(double factor) = 0;
  virtual double zoomFactor() const = 0;
};


#endif
