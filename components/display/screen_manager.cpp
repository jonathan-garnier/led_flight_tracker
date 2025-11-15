#include "screen_manager.h"

ScreenManager::ScreenManager(LEDMatrix& matrix)
    : _matrix(matrix)
{
}

void ScreenManager::addScreen(BaseScreen* screen)
{
    _screens.push_back(screen);

    // If this is the first screen, enter it
    if (_currentIndex == -1)
    {
        _currentIndex = 0;
        _screens[0]->onEnter();
    }
}

void ScreenManager::nextScreen()
{
    if (_screens.empty()) return;

    // Exit current
    _screens[_currentIndex]->onExit();

    // Move index
    _currentIndex = (_currentIndex + 1) % _screens.size();

    // Enter new
    _screens[_currentIndex]->onEnter();
}

void ScreenManager::previousScreen()
{
    if (_screens.empty()) return;

    _screens[_currentIndex]->onExit();

    _currentIndex--;
    if (_currentIndex < 0)
        _currentIndex = _screens.size() - 1;

    _screens[_currentIndex]->onEnter();
}

BaseScreen* ScreenManager::current()
{
    if (_currentIndex >= 0 && _currentIndex < (int)_screens.size())
        return _screens[_currentIndex];
    return nullptr;
}

void ScreenManager::update(float dt)
{
    if (auto* s = current())
        s->update(dt);
}

void ScreenManager::render()
{
    if (auto* s = current())
        s->render(_matrix);
}
