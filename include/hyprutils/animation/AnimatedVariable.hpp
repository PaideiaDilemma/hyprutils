#pragma once

#include "AnimationConfig.hpp"
#include "../memory/WeakPtr.hpp"
#include "../memory/SharedPtr.hpp"
#include "../signal/Signal.hpp"

#include <functional>
#include <chrono>

namespace Hyprutils {
    namespace Animation {
        class CAnimationManager;

        struct SAnimVarEvents {
            Signal::CSignal connect;
            Signal::CSignal forceDisconnect;
            Signal::CSignal lazyDisconnect;
        };

        /* A base class for animated variables. */
        class CBaseAnimatedVariable {
          public:
            using CallbackFun = std::function<void(Memory::CWeakPointer<CBaseAnimatedVariable> thisptr)>;

            CBaseAnimatedVariable() {
                ; // m_bDummy = true;
            };

            void create(int, Memory::CSharedPointer<CBaseAnimatedVariable>, Memory::CSharedPointer<SAnimVarEvents> events);
            void connectToActive();
            void disconnectFromActive();

            /* Needs to call disconnectFromActive to remove `m_pSelf` from the active animation list */
            virtual ~CBaseAnimatedVariable() {
                disconnectFromActive();
            };

            virtual void warp(bool endCallback = true, bool forceDisconnect = true) = 0;

            CBaseAnimatedVariable(const CBaseAnimatedVariable&)            = delete;
            CBaseAnimatedVariable(CBaseAnimatedVariable&&)                 = delete;
            CBaseAnimatedVariable& operator=(const CBaseAnimatedVariable&) = delete;
            CBaseAnimatedVariable& operator=(CBaseAnimatedVariable&&)      = delete;

            void                   setConfig(Memory::CSharedPointer<SAnimationPropertyConfig> pConfig) {
                m_pConfig = pConfig;
            }

            Memory::CWeakPointer<SAnimationPropertyConfig> getConfig() const {
                return m_pConfig;
            }

            bool               enabled() const;
            const std::string& getBezierName() const;
            const std::string& getStyle() const;

            /* returns the spent (completion) % */
            float getPercent() const;

            /* returns the current curve value.
               needs a reference to the animationmgr to get the bezier curve with the configured name from it */
            float getCurveValue(CAnimationManager*) const;

            /* checks if an animation is in progress */
            bool isBeingAnimated() const {
                return m_bIsBeingAnimated;
            }

            /* checks m_bDummy and m_pAnimationManager */
            bool ok() const;

            /* calls the update callback */
            void onUpdate();

            /* sets a function to be ran when an animation ended.
               if "remove" is set to true, it will remove the callback when ran. */
            void setCallbackOnEnd(CallbackFun func, bool remove = true);

            /* sets a function to be ran when an animation is started.
               if "remove" is set to true, it will remove the callback when ran. */
            void setCallbackOnBegin(CallbackFun func, bool remove = true);

            /* sets the update callback, called every time the value is animated and a step is done
               Warning: calling unregisterVar/registerVar in this handler will cause UB */
            void setUpdateCallback(CallbackFun func);

            /* resets all callbacks. Does not call any. */
            void resetAllCallbacks();

            void onAnimationEnd();
            void onAnimationBegin();

            int  m_Type = -1;

          protected:
            friend class CAnimationManager;

            bool                                        m_bIsConnectedToActive = false;
            bool                                        m_bIsBeingAnimated     = false;

            Memory::CWeakPointer<CBaseAnimatedVariable> m_pSelf;

            Memory::CWeakPointer<SAnimVarEvents>        m_events;

          private:
            Memory::CWeakPointer<SAnimationPropertyConfig> m_pConfig;

            std::chrono::steady_clock::time_point          animationBegin;

            bool                                           m_bDummy = true;

            bool                                           m_bRemoveEndAfterRan   = true;
            bool                                           m_bRemoveBeginAfterRan = true;

            CallbackFun                                    m_fEndCallback;
            CallbackFun                                    m_fBeginCallback;
            CallbackFun                                    m_fUpdateCallback;
        };

        /* This concept represents the minimum requirement for a type to be used with CGenericAnimatedVariable */
        template <class ValueImpl>
        concept AnimatedType = requires(ValueImpl val) {
            requires std::is_copy_constructible_v<ValueImpl>;
            { val == val } -> std::same_as<bool>; // requires operator==
            { val = val };                        // requires operator=
        };

        /*
            A generic class for variables.
            VarType is the type of the variable to be animated.
            AnimationContext is there to attach additional data to the animation.
            In Hyprland that struct would contain a reference to window, workspace or layer for example.
        */
        template <AnimatedType VarType, class AnimationContext>
        class CGenericAnimatedVariable : public CBaseAnimatedVariable {
          public:
            CGenericAnimatedVariable() = default;

            void create(const int typeInfo, Memory::CSharedPointer<CGenericAnimatedVariable<VarType, AnimationContext>> pSelf, Memory::CSharedPointer<SAnimVarEvents> events,
                        const VarType& initialValue) {
                m_Begun = initialValue;
                m_Value = initialValue;
                m_Goal  = initialValue;

                CBaseAnimatedVariable::create(typeInfo, pSelf, events);
            }

            CGenericAnimatedVariable(const CGenericAnimatedVariable&)            = delete;
            CGenericAnimatedVariable(CGenericAnimatedVariable&&)                 = delete;
            CGenericAnimatedVariable& operator=(const CGenericAnimatedVariable&) = delete;
            CGenericAnimatedVariable& operator=(CGenericAnimatedVariable&&)      = delete;

            virtual void              warp(bool endCallback = true, bool forceDisconnect = true) {
                if (!m_bIsBeingAnimated)
                    return;

                m_Value = m_Goal;

                onUpdate();

                m_bIsBeingAnimated = false;

                if (endCallback)
                    onAnimationEnd();

                if (forceDisconnect) {
                    if (const auto PEVENTS = m_events.lock())
                        PEVENTS->forceDisconnect.emit(static_cast<void*>(this));
                }
            }

            const VarType& value() const {
                return m_Value;
            }

            /* used to update the value each tick via the AnimationManager */
            VarType& value() {
                return m_Value;
            }

            const VarType& goal() const {
                return m_Goal;
            }

            const VarType& begun() const {
                return m_Begun;
            }

            CGenericAnimatedVariable& operator=(const VarType& v) {
                if (v == m_Goal)
                    return *this;

                m_Goal  = v;
                m_Begun = m_Value;

                onAnimationBegin();

                return *this;
            }

            /* Sets the actual stored value, without affecting the goal, but resets the timer*/
            void setValue(const VarType& v) {
                if (v == m_Value)
                    return;

                m_Value = v;
                m_Begun = m_Value;

                onAnimationBegin();
            }

            /* Sets the actual value and goal*/
            void setValueAndWarp(const VarType& v) {
                m_Goal             = v;
                m_bIsBeingAnimated = true;

                warp();
            }

            AnimationContext m_Context;

          private:
            VarType m_Value{};
            VarType m_Goal{};
            VarType m_Begun{};
        };
    }
}
