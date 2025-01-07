#include <hyprutils/animation/AnimationConfig.hpp>
#include <hyprutils/animation/AnimationManager.hpp>
#include <hyprutils/animation/AnimatedVariable.hpp>
#include <hyprutils/memory/WeakPtr.hpp>
#include "shared.hpp"

#define SP CSharedPointer
#define WP CWeakPointer

using namespace Hyprutils::Animation;
using namespace Hyprutils::Math;
using namespace Hyprutils::Memory;

class EmtpyContext {};

template <typename VarType>
using CAnimatedVariable = CGenericAnimatedVariable<VarType, EmtpyContext>;

template <typename VarType>
using PANIMVAR = SP<CAnimatedVariable<VarType>>;

template <typename VarType>
using PANIMVARREF = WP<CAnimatedVariable<VarType>>;

enum eAVTypes {
    INT = 1,
    TEST,
};

struct SomeTestType {
    bool done = false;
    bool operator==(const SomeTestType& other) const {
        return done == other.done;
    }
    SomeTestType& operator=(const SomeTestType& other) {
        done = other.done;
        return *this;
    }
};

CAnimationConfigTree animationTree;

class CMyAnimationManager : public CAnimationManager {
  public:
    void tick() {
        for (const auto& av : m_vActiveAnimatedVariables) {
            const auto PAV = av.lock();
            if (!PAV || !PAV->ok())
                continue;

            const auto SPENT   = PAV->getPercent();
            const auto PBEZIER = getBezier(PAV->getBezierName());

            if (SPENT >= 1.f || !PAV->enabled()) {
                PAV->warp();
                continue;
            }

            const auto POINTY = PBEZIER->getYForPoint(SPENT);

            switch (PAV->m_Type) {
                case eAVTypes::INT: {
                    auto avInt = dynamic_cast<CAnimatedVariable<int>*>(PAV.get());
                    if (!avInt)
                        std::cout << Colors::RED << "Dynamic cast upcast failed" << Colors::RESET;

                    const auto DELTA = avInt->goal() - avInt->value();
                    avInt->value()   = avInt->begun() + (DELTA * POINTY);
                } break;
                case eAVTypes::TEST: {
                    auto avCustom = dynamic_cast<CAnimatedVariable<SomeTestType>*>(PAV.get());
                    if (!avCustom)
                        std::cout << Colors::RED << "Dynamic cast upcast failed" << Colors::RESET;

                    if (SPENT >= 1.f)
                        avCustom->value().done = true;
                } break;
                default: {
                    std::cout << Colors::RED << "What are we even doing?" << Colors::RESET;
                } break;
            }
        }

        tickDone();
    }

    template <typename VarType>
    void createAnimation(const VarType& v, PANIMVAR<VarType>& av, const std::string& animationConfigName) {
        constexpr const eAVTypes EAVTYPE = std::is_same_v<VarType, int> ? eAVTypes::INT : eAVTypes::TEST;
        const auto               PAV     = makeShared<CGenericAnimatedVariable<VarType, EmtpyContext>>();

        PAV->create(EAVTYPE, static_cast<CAnimationManager*>(this), PAV, v);
        PAV->setConfig(animationTree.getConfig(animationConfigName));
        av = std::move(PAV);
    }

    virtual void scheduleTick() {
        ;
    }

    virtual void onTicked() {
        ;
    }
};

CMyAnimationManager gAnimationManager;

class Subject {
  public:
    Subject(const int& a, const int& b) {
        gAnimationManager.createAnimation(a, m_iA, "default");
        gAnimationManager.createAnimation(b, m_iB, "internal");
        gAnimationManager.createAnimation({}, m_iC, "default");
    }
    PANIMVAR<int>          m_iA;
    PANIMVAR<int>          m_iB;
    PANIMVAR<SomeTestType> m_iC;
};

int config() {
    int ret = 0;

    animationTree.createNode("global");
    animationTree.createNode("internal");

    animationTree.createNode("foo", "internal");
    animationTree.createNode("default", "global");
    animationTree.createNode("bar", "default");

    /*
      internal
        ↳ foo
      global
        ↳ default
          ↳ bar
    */

    auto barCfg      = animationTree.getConfig("bar");
    auto internalCfg = animationTree.getConfig("internal");

    // internal is a root node and should point to itself
    EXPECT(internalCfg->pParentAnimation.get(), internalCfg.get());
    EXPECT(internalCfg->pValues.get(), internalCfg.get());

    animationTree.setConfigForNode("global", 1, 4.0, "default", "asdf");

    EXPECT(barCfg->internalEnabled, -1);
    {
        const auto PVALUES = barCfg->pValues.lock();
        EXPECT(PVALUES->internalEnabled, 1);
        EXPECT(PVALUES->internalBezier, "default");
        EXPECT(PVALUES->internalStyle, "asdf");
        EXPECT(PVALUES->internalSpeed, 4.0);
    }
    EXPECT(barCfg->pParentAnimation.get(), animationTree.getConfig("default").get());

    // Overwrite our own values
    animationTree.setConfigForNode("bar", 1, 4.2, "test", "qwer");

    {
        const auto PVALUES = barCfg->pValues.lock();
        EXPECT(PVALUES->internalEnabled, 1);
        EXPECT(PVALUES->internalBezier, "test");
        EXPECT(PVALUES->internalStyle, "qwer");
        EXPECT(PVALUES->internalSpeed, 4.2f);
    }

    // Now overwrite the parent
    animationTree.setConfigForNode("default", 0, 0.0, "zxcv", "foo");

    {
        // Expecting no change
        const auto PVALUES = barCfg->pValues.lock();
        EXPECT(PVALUES->internalEnabled, 1);
        EXPECT(PVALUES->internalBezier, "test");
        EXPECT(PVALUES->internalStyle, "qwer");
        EXPECT(PVALUES->internalSpeed, 4.2f);
    }

    return ret;
}

int main(int argc, char** argv, char** envp) {
    int ret = config();

    animationTree.createNode("global");
    animationTree.createNode("internal");

    animationTree.createNode("default", "global");
    animationTree.setConfigForNode("global", 1, 4.0, "default", "asdf");

    Subject s(0, 0);

    EXPECT(s.m_iA->value(), 0);
    EXPECT(s.m_iB->value(), 0);

    // Test destruction of a CAnimatedVariable
    {
        Subject s2(10, 10);
        // Adds them to active
        *s2.m_iA = 1;
        *s2.m_iB = 2;
        // We deliberately do not tick here, to make sure the destructor removes active animated variables
    }

    EXPECT(gAnimationManager.shouldTickForNext(), false);
    EXPECT(s.m_iC->value().done, false);

    *s.m_iA = 10;
    *s.m_iB = 100;
    *s.m_iC = SomeTestType(true);

    EXPECT(s.m_iC->value().done, false);

    while (gAnimationManager.shouldTickForNext()) {
        gAnimationManager.tick();
    }

    EXPECT(s.m_iA->value(), 10);
    EXPECT(s.m_iB->value(), 100);
    EXPECT(s.m_iC->value().done, true);

    s.m_iA->setValue(0);
    s.m_iB->setValue(0);

    while (gAnimationManager.shouldTickForNext()) {
        gAnimationManager.tick();
    }

    EXPECT(s.m_iA->value(), 10);
    EXPECT(s.m_iB->value(), 100);

    // Test config stuff
    EXPECT(s.m_iA->getBezierName(), "default");
    EXPECT(s.m_iA->getStyle(), "asdf");
    EXPECT(s.m_iA->enabled(), true);

    animationTree.getConfig("global")->internalEnabled = 0;

    EXPECT(s.m_iA->enabled(), false);

    *s.m_iA = 50;
    gAnimationManager.tick(); // Expecting a warp
    EXPECT(s.m_iA->value(), 50);

    // Test missing pValues
    animationTree.getConfig("global")->internalEnabled = 0;
    animationTree.getConfig("default")->pValues.reset();

    EXPECT(s.m_iA->enabled(), false);
    EXPECT(s.m_iA->getBezierName(), "default");
    EXPECT(s.m_iA->getStyle(), "");
    EXPECT(s.m_iA->getPercent(), 1.f);

    // Reset
    animationTree.setConfigForNode("default", 1, 1, "default");

    //
    // Test callbacks
    //
    int beginCallbackRan  = 0;
    int updateCallbackRan = 0;
    int endCallbackRan    = 0;
    s.m_iA->setCallbackOnBegin([&beginCallbackRan](SP<CBaseAnimatedVariable> pav) { beginCallbackRan++; }, false);
    s.m_iA->setUpdateCallback([&updateCallbackRan](SP<CBaseAnimatedVariable> pav) { updateCallbackRan++; });
    s.m_iA->setCallbackOnEnd([&endCallbackRan](SP<CBaseAnimatedVariable> pav) { endCallbackRan++; }, false);

    EXPECT(gAnimationManager.shouldTickForNext(), false);

    s.m_iA->setValueAndWarp(42);

    EXPECT(gAnimationManager.shouldTickForNext(), false);

    EXPECT(beginCallbackRan, 0);
    EXPECT(updateCallbackRan, 1);
    EXPECT(endCallbackRan, 2); // first called when setting the callback, then when warping.

    *s.m_iA = 1337;
    while (gAnimationManager.shouldTickForNext()) {
        gAnimationManager.tick();
    }

    EXPECT(beginCallbackRan, 1);
    EXPECT(updateCallbackRan > 2, true);
    EXPECT(endCallbackRan, 3);

    std::vector<PANIMVAR<int>> vars;
    for (int i = 0; i < 10; i++) {
        vars.resize(vars.size() + 1);
        gAnimationManager.createAnimation(1, vars.back(), "default");
        *vars.back() = 1337;
    }

    // test adding / removing vars during a tick
    s.m_iA->resetAllCallbacks();
    s.m_iA->setUpdateCallback([&vars](SP<CBaseAnimatedVariable> v) {
        if (v != vars.back())
            vars.back()->warp();
    });
    s.m_iA->setCallbackOnEnd([&s, &vars](auto) {
        vars.resize(vars.size() + 1);
        gAnimationManager.createAnimation(1, vars.back(), "default");
        *vars.back() = 1337;
    });

    *s.m_iA = 1000000;

    while (gAnimationManager.shouldTickForNext()) {
        gAnimationManager.tick();
    }

    EXPECT(s.m_iA->value(), 1000000);
    // all vars should be set to 1337
    EXPECT(std::find_if(vars.begin(), vars.end(), [](const auto& v) { return v->value() != 1337; }) == vars.end(), true);
    EXPECT(endCallbackRan, 3);

    // test one-time callbacks
    s.m_iA->resetAllCallbacks();
    s.m_iA->setCallbackOnEnd([&endCallbackRan](auto) { endCallbackRan++; }, true);

    EXPECT(endCallbackRan, 4);

    s.m_iA->setValueAndWarp(10);

    EXPECT(endCallbackRan, 4);
    EXPECT(s.m_iA->value(), 10);

    return ret;
}
