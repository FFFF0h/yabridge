// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
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

#include <iostream>
#include <map>
#include <shared_mutex>
#include <string>

#include <clap/plugin.h>

#include "../../common/audio-shm.h"
#include "../../common/communication/clap.h"
#include "../../common/configuration.h"
#include "../../common/mutual-recursion.h"
#include "../editor.h"
#include "common.h"

/**
 * Pointers to all of a CLAP plugin's extension structs. These will be null if
 * the plugin doesn't support the extensions.
 *
 * @relates ClapPluginInstance
 */
struct ClapPluginExtensions {
    /**
     * Query all of the plugin's extensions. This can only be done after the
     * call to init.
     *
     * TODO: Return the supported extensions along with the init call.
     */
    ClapPluginExtensions(const clap_plugin& plugin) noexcept;

    // TODO: Supported extensions
};

/**
 * A CLAP plugin instance. This is created when the plugin is created from the
 * plugin factory. Dropping this object will also destroy the plugin instance,
 * but it will still need to be manually unregistered from the `ClapBridge`'s
 * object instance map. The extensions object is queried after the host calls
 * the init function. Before that time all extension pointers will be null
 * pointers.
 */
struct ClapPluginInstance {
    /**
     * Bind a CLAP plugin pointer to this plugin instance object. This can only
     * be done once per plugin pointer. The pointer must be non-null.
     */
    ClapPluginInstance(const clap_plugin* plugin) noexcept;

   public:
    /**
     * A dedicated thread for handling incoming audio thread function calls.
     */
    Win32Thread audio_thread_handler;

    // TODO: Proxies for host extension objects

    /**
     * A shared memory object we'll write the input audio buffers to on the
     * native plugin side. We'll then let the plugin write its outputs here on
     * the Wine side. The buffer will be configured during
     * `clap_plugin::activate()`. At that point we'll build the configuration
     * for the object here, on the Wine side, and then we'll initialize the
     * buffers using that configuration. This same configuration is then used on
     * the native plugin side to connect to this same shared memory object for
     * the matching plugin instance.
     */
    std::optional<AudioShmBuffer> process_buffers;

    /**
     * Pointers to the per-bus input channels in process_buffers so we can pass
     * them to the plugin after a call to `YaProcessData::reconstruct()`. These
     * can be either `float*` or `double*`, so we sadly have to use void
     * pointers here.
     *
     * FIXME: Update docstring for CLAP
     */
    std::vector<std::vector<void*>> process_buffers_input_pointers;

    /**
     * Pointers to the per-bus output channels in process_buffers so we can pass
     * them to the plugin after a call to `YaProcessData::reconstruct()`. These
     * can be either `float*` or `double*`, so we sadly have to use void
     * pointers here.
     *
     * FIXME: Update docstring for CLAP
     */
    std::vector<std::vector<void*>> process_buffers_output_pointers;

    /**
     * This instance's editor, if it has an open editor. Embedding here works
     * exactly the same as how it works for VST2 plugins.
     */
    std::optional<Editor> editor;

    /**
     * The plugin object. The plugin gets destroyed together with this struct.
     */
    std::unique_ptr<const clap_plugin, decltype(clap_plugin::destroy)> plugin;

    /**
     * Contains the plugin's supported extensions. Initialized after the host
     * calls `clap_plugin::init()`.
     */
    ClapPluginExtensions extensions;

    /**
     * Whether `clap_plugin::init()` has already been called for this object
     * instance. Some VST2 and VST3 plugins would have memory errors if the
     * Win32 message loop is run in between creating the plugin and initializing
     * it, so we're also preventing this for CLAP as a precaution.
     */
    bool is_initialized = false;

    // TODO: Add this when we add support for audio processing
    // /**
    //  * The plugin's current process setup, containing information about the
    //  * buffer sizes, sample rate, and processing mode. Used for setting up
    //  * shared memory audio buffers, and to know whether the plugin instance
    //  is
    //  * currently in offline processing mode or not. The latter is needed as a
    //  * HACK for IK Multimedia's T-RackS 5 because those plugins will deadlock
    //  if
    //  * they don't process audio from the GUI thread while doing offline
    //  * processing.
    //  */
    // std::optional<Steinberg::Vst::ProcessSetup> process_setup;
};

/**
 * This hosts a Windows CLAP plugin, forwards messages sent by the Linux CLAP
 * plugin and provides host callback function for the plugin to talk back.
 */
class ClapBridge : public HostBridge {
   public:
    /**
     * Initializes the Windows CLAP plugin and set up communication with the
     * native Linux CLAP plugin.
     *
     * @param main_context The main IO context for this application. Most events
     *   will be dispatched to this context, and the event handling loop should
     *   also be run from this context.
     * @param plugin_dll_path A (Unix style) path to the Windows .clap file to
     *   load. In yabridgectl we'll create symlinks to these using a `.clap-win`
     *   file extension as CLAP uses the same file extension on Windows and
     *   Linux.
     * @param endpoint_base_dir The base directory used for the socket
     *   endpoints. See `Sockets` for more information.
     * @param parent_pid The process ID of the native plugin host this bridge is
     *   supposed to communicate with. Used as part of our watchdog to prevent
     *   dangling Wine processes.
     *
     * @note The object has to be constructed from the same thread that calls
     *   `main_context.run()`.
     *
     * @throw std::runtime_error Thrown when the CLAP plugin could not be
     *   loaded, or if communication could not be set up.
     */
    ClapBridge(MainContext& main_context,
               std::string plugin_dll_path,
               std::string endpoint_base_dir,
               pid_t parent_pid);

    /**
     * This returns `true` if `clap_plugin::init()` has not yet been called for
     * any of the registered plugins. Some VST2 and VST3 plugins have memory
     * errors if the Win32 message loop is pumped before init is called, so
     * we'll just keep the same behaviour for CLAP just in case.
     */
    bool inhibits_event_loop() noexcept override;

    /**
     * Here we'll listen for and handle incoming control messages until the
     * sockets get closed.
     */
    void run() override;

    // TODO: Editor resizing
    // /**
    //  * If the plugin instance has an editor, resize the wrapper window to
    //  match
    //  * the new size. This is called from `IPlugFrame::resizeView()` to make
    //  sure
    //  * we do the resize before the request gets sent to the host.
    //  */
    // bool maybe_resize_editor(size_t instance_id,
    //                          const Steinberg::ViewRect& new_size);

   protected:
    void close_sockets() override;

   public:
    /**
     * Send a callback message to the host return the response. This is a
     * shorthand for `sockets.plugin_host_callback_.send_message` for use in
     * CLAP interface implementations.
     */
    template <typename T>
    typename T::Response send_message(const T& object) {
        return sockets_.plugin_host_main_thread_callback_.send_message(
            object, std::nullopt);
    }

    /**
     * When called from the GUI thread, spawn a new thread and call
     * `send_message()` from there, and then handle functions passed by calls to
     * `do_mutual_recursion_on_gui_thread()` and
     * `do_mutual_recursion_on_off_thread()` on this thread until we get a
     * response back. See the function in `Vst3Bridge` for a much more in-depth
     * explanatio nof why this is neede.d
     *
     * TODO: Is this needed for CLAP?
     */
    template <typename T>
    typename T::Response send_mutually_recursive_message(const T& object) {
        if (main_context_.is_gui_thread()) {
            return mutual_recursion_.fork(
                [&]() { return send_message(object); });
        } else {
            // TODO: Remove if this isn't needed
            logger_.log_trace([]() {
                return "'ClapBridge::send_mutually_recursive_message()' called "
                       "from a non-GUI thread, sending the message directly";
            });
            send_message(object);

            // return audio_thread_mutual_recursion_.fork(
            //     [&]() { return send_message(object); });
        }
    }

    /**
     * Crazy functions ask for crazy naming. This is the other part of
     * `send_mutually_recursive_message()`, for executing mutually recursive
     * functions on the GUI thread. If another thread is currently calling that
     * function (from the UI thread), then we'll execute `fn` from the UI thread
     * using the IO context started in the above function. Otherwise `f` will be
     * run on the UI thread through `main_context_` as usual.
     *
     * @see ClapBridge::send_mutually_recursive_message
     */
    template <std::invocable F>
    std::invoke_result_t<F> do_mutual_recursion_on_gui_thread(F&& fn) {
        // If the above function is currently being called from some thread,
        // then we'll call `fn` from that same thread. Otherwise we'll just
        // submit it to the main IO context.
        if (const auto result =
                mutual_recursion_.maybe_handle(std::forward<F>(fn))) {
            return *result;
        } else {
            return main_context_.run_in_context(std::forward<F>(fn)).get();
        }
    }

    // TODO: Check if this is needed, remove if it isn't

    // /**
    //  * The same as the above function, but we'll just execute the function on
    //  * this thread when the mutual recursion context is not active.
    //  *
    //  * @see ClapBridge::do_mutual_recursion_on_gui_thread
    //  */
    // template <std::invocable F>
    // std::invoke_result_t<F> do_mutual_recursion_on_off_thread(F&& fn) {
    //     if (const auto result = audio_thread_mutual_recursion_.maybe_handle(
    //             std::forward<F>(fn))) {
    //         return *result;
    //     } else {
    //         return mutual_recursion_.handle(std::forward<F>(fn));
    //     }
    // }

    /**
     * A logger instance we'll use to log about failed
     * `clap_host::get_extension()` calls, so they can be hidden on verbosity
     * level 0.
     *
     * This only has to be used instead of directly writing to `std::cerr` when
     * the message should be hidden on lower verbosity levels.
     *
     * TODO: Actually use this
     */
    ClapLogger logger_;

   private:
    /**
     * Generate a nique instance identifier using an atomic fetch-and-add. This
     * is used to be able to refer to specific plugin instances in the messages.
     */
    size_t generate_instance_id() noexcept;

    /**
     * Fetch the plugin instance along with a lock valid for the instance's
     * lifetime. This is mostly just to save some boilerplate everywhere. Use
     * C++17's structured binding as syntactic sugar to not have to deal with
     * the lock handle.
     */
    std::pair<ClapPluginInstance&, std::shared_lock<std::shared_mutex>>
    get_instance(size_t instance_id) noexcept;

    /**
     * Sets up the shared memory audio buffers for a plugin instance plugin
     * instance and return the configuration so the native plugin can connect to
     * it as well.
     *
     * This returns a nullopt when the buffer size parameters passed to
     * `clap_plugin::activate()` have not yet been set in the
     * `ClapPluginInstance`.
     *
     * A nullopt will also be returned if this is called again after shared
     * audio buffers have been set up and the audio buffer size has not changed.
     */
    std::optional<AudioShmBuffer::Config> setup_shared_audio_buffers(
        size_t instance_id);

    /**
     * Assign a unique identifier to an object and add it to
     * `object_instances_`. This will also set up an audio thread socket
     * listener for the plugin instance.
     */
    size_t register_plugin_instance(const clap_plugin* plugin);

    /**
     * Remove an object from `object_instances_`. Will also tear down the
     * instance's audio thread.
     */
    void unregister_object_instance(size_t instance_id);

    /**
     * The configuration for this instance of yabridge based on the path to the
     * `.so` (or well `.clap`) file that got loaded by the host. This
     * configuration gets loaded on the plugin side, and then sent over to the
     * Wine host as part of the startup process.
     */
    Configuration config_;

    // TODO: Grab the entry point and factory
    // std::shared_ptr<CLAP::Hosting::Module> module_;

    /**
     * All sockets used for communicating with this specific plugin.
     *
     * NOTE: This is defined **after** the threads on purpose. This way the
     *       sockets will be closed first, and we can then safely wait for the
     *       threads to exit.
     */
    ClapSockets<Win32Thread> sockets_;

    /**
     * Used to assign a unique identifier to created plugin instances so they
     * can be referred to later.
     *
     * @related generate_instance_id
     */
    std::atomic_size_t current_instance_id_;

    /**
     * These are all the objects we have created through the Windows CLAP
     * plugins' plugin factory. The keys in all of these maps are the unique
     * identifiers we generated for them so we can identify specific instances.
     * During the proxy object's destructor (on the plugin side), we'll get a
     * request to remove the corresponding plugin object from this map. This
     * will cause all pointers to it to get dropped and the object to be cleaned
     * up.
     */
    std::unordered_map<size_t, ClapPluginInstance> object_instances_;
    /**
     * In theory all object handling is safe iff the host also doesn't do
     * anything weird even without locks. The only time a data race can occur is
     * when the host removes or inserts a plugin while also interacting with
     * other plugins on different threads. Since the lock should never be
     * contested, we should also not get a measurable performance penalty from
     * making double sure nothing can go wrong.
     *
     * TODO: At some point replace this with a multiple reader single writer
     *       lock based by a spinlock. Because this lock is rarely contested
     *       `get_instance()` never yields to the scheduler during audio
     *       processing, but it's still something we should avoid at all costs.
     */
    std::shared_mutex object_instances_mutex_;

    /**
     * Used in `send_mutually_recursive_message()` to be able to execute
     * functions from that same calling thread (through
     * `do_mutual_recursion_on_gui_thread()` and
     * `do_mutual_recursion_on_off_thread()`) while we're waiting for a
     * response.
     */
    MutualRecursionHelper<Win32Thread> mutual_recursion_;

    // TODO: Check if this is needed, remove it if it isn't
    // /**
    //  * The same thing as above, but just for the pair of
    //  * `IEditController::setParamNormalized()` and
    //  * `IComponentHandler::performEdit()`, when
    //  * `IComponentHandler::performEdit()` is called from an audio thread.
    //  *
    //  * HACK: This is sadly needed to work around an interaction between a bug
    //  in
    //  *       JUCE with a bug in Ardour/Mixbus. JUCE calls
    //  *       `IComponentHandler::performEdit()` from the audio thread instead
    //  of
    //  *       using the output parameters, and Ardour/Mixbus immediately call
    //  *       `IEditController::setParamNormalized()` with the same value
    //  after
    //  *       the plugin calls `IComponentHandler::performEdit()`. Both of
    //  these
    //  *       functions need to be run on the same thread (because of
    //  recursive
    //  *       mutexes), but they may not interfere with the GUI thread if
    //  *       `IComponentHandler::performEdit()` wasn't called from there.
    //  */
    // MutualRecursionHelper<Win32Thread> audio_thread_mutual_recursion_;
};
