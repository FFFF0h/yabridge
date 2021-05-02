// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "../vst3.h"
#include "plug-view-proxy.h"

/**
 * Here we pass though all function calls made by the host to the Windows VST3
 * plugin. We sadly had to deviate from yabridge's 'one-to-one passthrough'
 * philosphy in two places:
 *
 * 1. We cache parameter information, and these caches are flushed whenever the
 *    plugin requests a restart. This is needed because REAPER repeatedly
 *    queries this information four times per second for all of a plugin's
 *    parameters while the editor is open. The issue has been reported and it's
 *    been fixed in REAPER's current pre-release builds (as of February 2021).
 *    Bitwig also seems to query this information twice on startup, so the cache
 *    is likely also useful there.
 * 2. We also cache input and output bus counts and information. REAPER would
 *    query this information for every I/O bus before processing audio, which
 *    ended up increasing audio processing latency considerably for no reason
 *    (since this information cannot change during processing). REAPER has fixed
 *    this issue as of a pre-release build in February 2021. JUCE based hosts
 *    like Carla also seem to query the bus counts every processing cycle.
 */
class Vst3PluginProxyImpl : public Vst3PluginProxy {
   public:
    Vst3PluginProxyImpl(Vst3PluginBridge& bridge,
                        Vst3PluginProxy::ConstructArgs&& args);

    /**
     * When the reference count reaches zero and this destructor is called,
     * we'll send a request to the Wine plugin host to destroy the corresponding
     * object.
     */
    ~Vst3PluginProxyImpl();

    /**
     * We'll override the query interface to log queries for interfaces we do
     * not (yet) support.
     */
    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override;

    /**
     * Add a context menu created by a call to
     * `IComponentHandler3::createContextMenu` to our list of registered cotnext
     * menus. This way we can refer to it later when the plugin calls a function
     * on the proxy object we'll create for it.
     */
    size_t register_context_menu(
        Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu);

    /**
     * Unregister a context menu using the ID generated by a previous call to
     * `register_context_menu()`. This will release the context menu object
     * returned by the host.
     */
    bool unregister_context_menu(size_t context_menu_id);

    /**
     * Clear the bus and parameter caches. We'll call this on
     * `IComponentHandler::restartComponent`. These caching layers are necessary
     * to get decent performance in REAPER as REAPER repeatedly calls these
     * functions many times per second, even though their values will never
     * change.
     *
     * HACK: See the doc comment on this class for more information on these
     *       caches
     *
     * @see clear_bus_cache
     * @see clear_parameter_cache
     */
    void clear_caches();

    // From `IAudioPresentationLatency`
    tresult PLUGIN_API
    setAudioPresentationLatencySamples(Steinberg::Vst::BusDirection dir,
                                       int32 busIndex,
                                       uint32 latencyInSamples) override;

    // From `IAudioProcessor`
    tresult PLUGIN_API
    setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                       int32 numIns,
                       Steinberg::Vst::SpeakerArrangement* outputs,
                       int32 numOuts) override;
    tresult PLUGIN_API
    getBusArrangement(Steinberg::Vst::BusDirection dir,
                      int32 index,
                      Steinberg::Vst::SpeakerArrangement& arr) override;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override;
    uint32 PLUGIN_API getLatencySamples() override;
    tresult PLUGIN_API
    setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    tresult PLUGIN_API setProcessing(TBool state) override;
    tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    uint32 PLUGIN_API getTailSamples() override;

    // From `IAutomationState`
    tresult PLUGIN_API setAutomationState(int32 state) override;

    // From `IComponent`
    tresult PLUGIN_API getControllerClassId(Steinberg::TUID classId) override;
    tresult PLUGIN_API setIoMode(Steinberg::Vst::IoMode mode) override;
    int32 PLUGIN_API getBusCount(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir) override;
    tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override;
    tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override;
    tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                   Steinberg::Vst::BusDirection dir,
                                   int32 index,
                                   TBool state) override;
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // From `IConnectionPoint`
    tresult PLUGIN_API connect(IConnectionPoint* other) override;
    tresult PLUGIN_API disconnect(IConnectionPoint* other) override;
    tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // From `IEditController`
    tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    // `IEditController` also contains `getState()` and `setState()`  functions.
    // These are identical to those defiend in `IComponent` and they're thus
    // handled in in the same function.
    int32 PLUGIN_API getParameterCount() override;
    tresult PLUGIN_API
    getParameterInfo(int32 paramIndex,
                     Steinberg::Vst::ParameterInfo& info /*out*/) override;
    tresult PLUGIN_API
    getParamStringByValue(Steinberg::Vst::ParamID id,
                          Steinberg::Vst::ParamValue valueNormalized /*in*/,
                          Steinberg::Vst::String128 string /*out*/) override;
    tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID id,
        Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::ParamValue& valueNormalized /*out*/) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    normalizedParamToPlain(Steinberg::Vst::ParamID id,
                           Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    plainParamToNormalized(Steinberg::Vst::ParamID id,
                           Steinberg::Vst::ParamValue plainValue) override;
    Steinberg::Vst::ParamValue PLUGIN_API
    getParamNormalized(Steinberg::Vst::ParamID id) override;
    tresult PLUGIN_API
    setParamNormalized(Steinberg::Vst::ParamID id,
                       Steinberg::Vst::ParamValue value) override;
    tresult PLUGIN_API
    setComponentHandler(Steinberg::Vst::IComponentHandler* handler) override;
    Steinberg::IPlugView* PLUGIN_API
    createView(Steinberg::FIDString name) override;

    // From `IEditController2`
    tresult PLUGIN_API setKnobMode(Steinberg::Vst::KnobMode mode) override;
    tresult PLUGIN_API openHelp(TBool onlyCheck) override;
    tresult PLUGIN_API openAboutBox(TBool onlyCheck) override;

    // From `IEditControllerHostEditing`
    tresult PLUGIN_API
    beginEditFromHost(Steinberg::Vst::ParamID paramID) override;
    tresult PLUGIN_API
    endEditFromHost(Steinberg::Vst::ParamID paramID) override;

    // From `IInfoListener`
    tresult PLUGIN_API
    setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override;

    // From `IKeyswitchController`
    int32 PLUGIN_API getKeyswitchCount(int32 busIndex, int16 channel) override;
    tresult PLUGIN_API
    getKeyswitchInfo(int32 busIndex,
                     int16 channel,
                     int32 keySwitchIndex,
                     Steinberg::Vst::KeyswitchInfo& info /*out*/) override;

    // From `IMidiLearn`
    tresult PLUGIN_API
    onLiveMIDIControllerInput(int32 busIndex,
                              int16 channel,
                              Steinberg::Vst::CtrlNumber midiCC) override;

    // From `IMidiMapping`
    tresult PLUGIN_API
    getMidiControllerAssignment(int32 busIndex,
                                int16 channel,
                                Steinberg::Vst::CtrlNumber midiControllerNumber,
                                Steinberg::Vst::ParamID& id /*out*/) override;

    // From `INoteExpressionController`
    int32 PLUGIN_API getNoteExpressionCount(int32 busIndex,
                                            int16 channel) override;
    tresult PLUGIN_API getNoteExpressionInfo(
        int32 busIndex,
        int16 channel,
        int32 noteExpressionIndex,
        Steinberg::Vst::NoteExpressionTypeInfo& info /*out*/) override;
    tresult PLUGIN_API getNoteExpressionStringByValue(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        Steinberg::Vst::NoteExpressionValue valueNormalized /*in*/,
        Steinberg::Vst::String128 string /*out*/) override;
    tresult PLUGIN_API getNoteExpressionValueByString(
        int32 busIndex,
        int16 channel,
        Steinberg::Vst::NoteExpressionTypeID id,
        const Steinberg::Vst::TChar* string /*in*/,
        Steinberg::Vst::NoteExpressionValue& valueNormalized /*out*/) override;

    // From `INoteExpressionPhysicalUIMapping`
    tresult PLUGIN_API
    getPhysicalUIMapping(int32 busIndex,
                         int16 channel,
                         Steinberg::Vst::PhysicalUIMapList& list) override;

    // From `IParameterFunctionName`
    tresult PLUGIN_API
    getParameterIDFromFunctionName(Steinberg::Vst::UnitID unitID,
                                   Steinberg::FIDString functionName,
                                   Steinberg::Vst::ParamID& paramID) override;

    // From `IPluginBase`
    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API terminate() override;

    // From `IPrefetchableSupport`
    tresult PLUGIN_API getPrefetchableSupport(
        Steinberg::Vst::PrefetchableSupport& prefetchable /*out*/) override;

    // From `IProcessContextRequirements`
    uint32 PLUGIN_API getProcessContextRequirements() override;

    // From `IProgramListData`
    tresult PLUGIN_API
    programDataSupported(Steinberg::Vst::ProgramListID listId) override;
    tresult PLUGIN_API getProgramData(Steinberg::Vst::ProgramListID listId,
                                      int32 programIndex,
                                      Steinberg::IBStream* data) override;
    tresult PLUGIN_API setProgramData(Steinberg::Vst::ProgramListID listId,
                                      int32 programIndex,
                                      Steinberg::IBStream* data) override;

    // From `IUnitData`
    tresult PLUGIN_API
    unitDataSupported(Steinberg::Vst::UnitID unitId) override;
    tresult PLUGIN_API getUnitData(Steinberg::Vst::UnitID unitId,
                                   Steinberg::IBStream* data) override;
    tresult PLUGIN_API setUnitData(Steinberg::Vst::UnitID unitId,
                                   Steinberg::IBStream* data) override;

    // From `IUnitInfo`
    int32 PLUGIN_API getUnitCount() override;
    tresult PLUGIN_API
    getUnitInfo(int32 unitIndex,
                Steinberg::Vst::UnitInfo& info /*out*/) override;
    int32 PLUGIN_API getProgramListCount() override;
    tresult PLUGIN_API
    getProgramListInfo(int32 listIndex,
                       Steinberg::Vst::ProgramListInfo& info /*out*/) override;
    tresult PLUGIN_API
    getProgramName(Steinberg::Vst::ProgramListID listId,
                   int32 programIndex,
                   Steinberg::Vst::String128 name /*out*/) override;
    tresult PLUGIN_API
    getProgramInfo(Steinberg::Vst::ProgramListID listId,
                   int32 programIndex,
                   Steinberg::Vst::CString attributeId /*in*/,
                   Steinberg::Vst::String128 attributeValue /*out*/) override;
    tresult PLUGIN_API
    hasProgramPitchNames(Steinberg::Vst::ProgramListID listId,
                         int32 programIndex) override;
    tresult PLUGIN_API
    getProgramPitchName(Steinberg::Vst::ProgramListID listId,
                        int32 programIndex,
                        int16 midiPitch,
                        Steinberg::Vst::String128 name /*out*/) override;
    Steinberg::Vst::UnitID PLUGIN_API getSelectedUnit() override;
    tresult PLUGIN_API selectUnit(Steinberg::Vst::UnitID unitId) override;
    tresult PLUGIN_API
    getUnitByBus(Steinberg::Vst::MediaType type,
                 Steinberg::Vst::BusDirection dir,
                 int32 busIndex,
                 int32 channel,
                 Steinberg::Vst::UnitID& unitId /*out*/) override;
    tresult PLUGIN_API setUnitProgramData(int32 listOrUnitId,
                                          int32 programIndex,
                                          Steinberg::IBStream* data) override;

    // From `IXmlRepresentationController`
    tresult PLUGIN_API
    getXmlRepresentationStream(Steinberg::Vst::RepresentationInfo& info /*in*/,
                               Steinberg::IBStream* stream /*out*/);

    /**
     * The component handler the host passed to us during
     * `IEditController::setComponentHandler()`. When the plugin makes a
     * callback on a component handler proxy object, we'll pass the call through
     * to this object.
     */
    Steinberg::IPtr<Steinberg::Vst::IComponentHandler> component_handler;

    /**
     * If the host places a proxy between two objects in
     * `IConnectionPoint::connect()`, we'll first try to bypass this proxy to
     * avoid a lot of edge cases with plugins that use these notifications from
     * the GUI thread. We'll do this by exchanging messages containing the
     * connected object's instance ID. If we can successfully exchange instance
     * IDs this way, we'll still connect the objects directly on the Wine plugin
     * host side. So far this is only needed for Ardour.
     */
    std::optional<size_t> connected_instance_id;

    /**
     * If we cannot manage to bypass the connection proxy as mentioned in the
     * docstring of `connected_instance_id`, then we'll store the host's
     * connection point proxy here and we'll proxy that proxy, if that makes any
     * sense.
     */
    Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> connection_point_proxy;

    /**
     * An unmanaged, raw pointer to the `IPlugView` instance returned in our
     * implementation of `IEditController::createView()`. We need this to handle
     * `IPlugFrame::resizeView()`, since that expects a pointer to the view that
     * gets resized.
     *
     * XXX: This approach of course won't work with multiple views, but the SDK
     *      currently only defines a single type of view so that shouldn't be an
     *      issue
     */
    Vst3PlugViewProxyImpl* last_created_plug_view = nullptr;

    /**
     * Whether `last_created_plug_view` is currently active. This field is
     * written to from `Vst3PlugViewProxyImpl`'s constructor and destructor.
     */
    std::atomic_bool last_created_plug_view_active = false;

    /**
     * A pointer to a context menu returned by the host as a response to a call
     * to `IComponentHandler3::createContextMenu`, as well as all targets we've
     * created for it. This way we can drop both all at once.
     */
    struct ContextMenu {
        ContextMenu(Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu);

        Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu;

        /**
         * All targets we pass to `IContextMenu::addItem`. We'll store them per
         * item tag, so we can drop them together with the menu. We probably
         * don't have to use smart pointers for this, but the docs are missing a
         * lot of details o how this should be implemented and there's no
         * example implementation around.
         */
        std::map<int32, Steinberg::IPtr<YaContextMenuTarget>> targets;
    };

    /**
     * All context menus created by this object through
     * `IComponentHandler3::createContextMenu()`. We'll generate a unique
     * identifier for each context menu just like we do for plugin objects. When
     * the plugin drops the context menu object, we'll also remove the
     * corresponding entry for this map causing the original pointer returned by
     * the host to get dropped a well.
     *
     * @see Vst3PluginProxyImpl::register_context_menu
     * @see Vst3PluginProxyImpl::unregister_context_menu
     */
    std::map<size_t, ContextMenu> context_menus;
    std::mutex context_menus_mutex;

    // The following pointers are cast from `host_context` if
    // `IPluginBase::initialize()` has been called

    Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication> host_application;
    Steinberg::FUnknownPtr<Steinberg::Vst::IPlugInterfaceSupport>
        plug_interface_support;

    // The following pointers are cast from `component_handler` if
    // `IEditController::setComponentHandler()` has been called

    Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler2>
        component_handler_2;
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler3>
        component_handler_3;
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandlerBusActivation>
        component_handler_bus_activation;
    Steinberg::FUnknownPtr<Steinberg::Vst::IProgress> progress;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitHandler> unit_handler;
    Steinberg::FUnknownPtr<Steinberg::Vst::IUnitHandler2> unit_handler_2;

   private:
    /**
     * Clear the bus count and information cache. We need this cache for REAPER
     * as it makes `num_inputs + num_outputs + 2` function calls to retrieve
     * this information every single processing cycle. For plugins with a lot of
     * outputs this really adds up. According to the VST3 workflow diagrams bus
     * information cannot change anymore once `IAudioProcessor::setProcessing()`
     * has been called, but REAPER doesn't quite follow the spec here and it
     * will set bus arrangements and activate the plugin only after it's called
     * `IAudioProcessor::setProcessing()`. Because of that we'll have to
     * manually flush this cache when the stores information potentially becomes
     * invalid.
     *
     * @see processing_bus_cache
     */
    void clear_bus_cache();

    /**
     * Clears the parameter information cache. Normally hosts only have to
     * request this once, since the information never changes. REAPER however in
     * some situations asks for this information four times per second. This
     * extra back and forth can really add up once plugins start having
     * thousands of parameters.
     *
     * @see parameter_info_cache
     */
    void clear_parameter_cache();

    /**
     * If we have an active `IPlugView` instance, try to use the mutual
     * recursion mechanism so that callbacks made by the plugin can be handled
     * on this same thread. In case this is an audio processor with a separate
     * edit controller, we'll also check if the object we're connected to has an
     * active `IPlugView` instance. When there's no active `IPlugView` instance,
     * we'll just send the event message like normal. This is needed to be able
     * to handle function calls made by the host (which is mostly relevant for
     * REAPER) on the GUI thread, when the plugin makes a callback to the host
     * that should also be handled on that same thread (context menus and
     * plugin-driven resizes).
     */
    template <typename T>
    typename T::Response maybe_send_mutually_recursive_message(
        const T& object) {
        if (last_created_plug_view_active) {
            return last_created_plug_view->send_mutually_recursive_message(
                std::move(object));
        } else if (connected_instance_id) {
            // We should also be able to handle the above situation when a
            // `setState()` on a processor triggers a resize coming from the
            // edit controller. To do that, we'll also check if the connected
            // instance has an active plug view.
            Vst3PluginProxyImpl& other_instance =
                bridge.plugin_proxies.at(*connected_instance_id).get();
            if (other_instance.last_created_plug_view_active) {
                return other_instance.last_created_plug_view
                    ->send_mutually_recursive_message(std::move(object));
            }
        }

        return bridge.send_message(std::move(object));
    }

    Vst3PluginBridge& bridge;

    /**
     * An host context if we get passed one through `IPluginBase::initialize()`.
     * We'll read which interfaces it supports and we'll then create a proxy
     * object that supports those same interfaces. This should be the same for
     * all plugin instances so we should not have to store it here separately,
     * but for the sake of correctness we will.
     */
    Steinberg::IPtr<Steinberg::FUnknown> host_context;

    /**
     * We'll periodically synchronize the Wine host's audio thread priority with
     * that of the host. Since the overhead from doing so does add up, we'll
     * only do this every once in a while.
     */
    time_t last_audio_thread_priority_synchronization = 0;

    /**
     * Used to assign unique identifiers to context menus created by
     * `IComponentHandler3::CreateContextMenu`.
     *
     * @related Vst3PluginProxyImpl::register_context_menu
     */
    std::atomic_size_t current_context_menu_id;

    /**
     * A cache for `IAudioProcessor::getBusCount()` and
     * `IAudioProcessor::getBusInfo()` to work around an implementation issue in
     * REAPER. If during processing a plugin returns a value for one of these
     * function calls, we'll memoize the function call using the maps defined
     * below.
     *
     * @see processing_bus_cache
     */
    struct BusInfoCache {
        std::map<
            std::tuple<Steinberg::Vst::MediaType, Steinberg::Vst::BusDirection>,
            int32>
            bus_count;
        std::map<std::tuple<Steinberg::Vst::MediaType,
                            Steinberg::Vst::BusDirection,
                            int32>,
                 Steinberg::Vst::BusInfo>
            bus_info;
    };

    /**
     * To work around some behaviour in REAPER where it will repeatedly query
     * the same bus information for bus during every processing cycle, we'll
     * cache this information during processing. Otherwise this will cause
     * `input_busses + output_busses + 2` extra unnecessary back and forths for
     * every processing cycle. This can really add up for plugins with 16, or
     * even 32 outputs.
     *
     * Since this information cannot change during processing, this will not
     * contain a value while the plugin is not processing audio.
     *
     * HACK: See the doc comment on this class
     */
    std::optional<BusInfoCache> processing_bus_cache;
    std::mutex processing_bus_cache_mutex;

    /**
     * A cache for `IEditController::getParameterCount()` and
     * `IEditController::getParameterInfo()` to work around an implementation
     * issue in REAPER. In some situations REAPER will query this information
     * four times a second, and all of this back and forth communication really
     * adds up when a plugin starts having thousands of parameters.
     *
     * @see parameter_cache
     */
    struct ParameterInfoCache {
        std::optional<int32> parameter_count;
        std::map<int32, Steinberg::Vst::ParameterInfo> parameter_info;
    };

    /**
     * A cache for the parameter count and infos. This is necessary because in
     * some situations REAPER queries this information four times per second
     * even though it cannot change. This happens when using the plugin bridges,
     * but it can also happen in some other cases so I'm not quite sure what the
     * trigger is.
     *
     * HACK: See the doc comment on this class
     */
    ParameterInfoCache parameter_info_cache;
    std::mutex parameter_info_cache_mutex;
};
