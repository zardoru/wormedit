# SDL presentation override

The application keeps SDL's GPU renderer abstraction. This override is applied
to the selected SDL tree as immutable input. Linux receives the Vulkan recovery
changes. Apple platforms receive only a native Metal timeout/nil-drawable guard;
Windows keeps its native SDL GPU backend. No Vulkan extension requirement is
added on Apple or Windows.

The audited SDL input is commit `e0b12bbcea659c5f60d7b1e7797902600b5a8a52`
(2026-06-29). `patch_bounded_presentation.py` checks both original translation
units by SHA-256 and requires each replacement anchor exactly once. CMake writes
private translation units under `build/wormedit-sdl-override/`, replaces only
those SDL target source entries, and retains the original include directories.
The Apple branch instead copies only `src/gpu/metal/SDL_gpu_metal.m` and keeps
SDL's Metal backend and presentation modes. Neither branch patches
`SDL3_SOURCE_DIR`. `manifest.json` records original and generated
hashes. An unaudited source version fails configuration before publishing an
override; upgrading SDL requires another source audit.

An upstream check on 2026-10-25 found the same blocking paths in both official
[SDL main at 14f79fa](https://github.com/libsdl-org/SDL/blob/14f79fa20e98800a6b9a276bd29697011c8a889b/src/render/gpu/SDL_render_gpu.c#L1579)
and [stable 3.4.18](https://github.com/libsdl-org/SDL/tree/release-3.4.18), released
2026-10-02. Updating SDL alone does not fix this issue.

## Linux behavior

- GPU presentation uses `SDL_AcquireGPUSwapchainTexture`; Vulkan acquisition
  has timeout zero. `VK_NOT_READY` and `VK_TIMEOUT` skip presentation. The current
  frame's offscreen work still submits, preserving texture updates.
- An acquisition performs at most one driver attempt. Out-of-date and lost
  surfaces schedule recovery for a later frame rather than looping.
- Swapchain recovery polls submitted GPU fences and presentation fences. It
  retains the existing swapchain and all its resources while either is pending.
  When safe, it reuses SDL's cleanup and recreation path. No retired-swapchain
  list grows during a stall. Surface recovery destroys the old swapchain before
  its surface, as Vulkan requires.
- The existing VSync setting and present-mode selection remain unchanged. In
  particular the application's VSync-on default still uses FIFO.
- Linux startup requires `VK_EXT_surface_maintenance1`,
  `VK_KHR_get_surface_capabilities2`, `VK_KHR_get_physical_device_properties2`,
  and `VK_EXT_swapchain_maintenance1` with `swapchainMaintenance1` enabled.
  Unsupported drivers fail with an explicit requirement message; there is no
  silent fallback to the old blocking path.

GPU submission completion alone does not prove that presentation resources are
safe to destroy. The
[presentation-fence contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)
provides that proof. This is why merely changing the acquire call or removing
`vkDeviceWaitIdle` would be incorrect. The
[surface lifetime rule](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySurfaceKHR.html)
also requires retiring the old swapchain first.

This bounds the application's acquire/recreate wait policy, not arbitrary broken
driver calls. Vulkan permits
[`vkQueuePresentKHR` to block for a finite time](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).
The application watchdog separately reports long driver calls. Explicit renderer
shutdown retains normal resource-destruction waits; live recovery never calls
device-idle or waits for unsignaled presentation fences.

Runtime properties are `wormedit.gpu.bounded_present` on the SDL GPU device and
renderer, and `wormedit.gpu.presentation_pending` on the window. The latter marks
a supported presentation attempt skipped because acquisition or recovery is not
ready.

## Checks

Source immutability, repeatability and rejected unaudited/ambiguous input:

```sh
python3 cmake/sdl/test_bounded_presentation.py --source build/_deps/sdl3-src
```

Build the small test-only dispatch shim separately from the application:

```sh
cc -std=c11 -shared -fPIC -I build/_deps/sdl3-src/src/video/khronos \
  cmake/sdl/presentation_fault_shim.c -ldl -o /tmp/wormedit-present-fault.so
```

Actual SDL/Vulkan execution with an already built application, using llvmpipe:

```sh
python3 cmake/sdl/test_bounded_presentation.py --source build/_deps/sdl3-src \
  --integration --executable build/wormedit --shim /tmp/wormedit-present-fault.so \
  --icd /usr/share/vulkan/icd.d/lvp_icd.json \
  --artifacts /tmp/wormedit-t293-supported
```

Repeat with `--expect-unsupported` and a different artifact directory to hide the
maintenance extension and verify explicit startup rejection. A run with
`--stall-seconds 2.8` holds the first fault long enough to check exactly one
watchdog stall/resume pair in stderr and the private recovery crash log. Recovery
notification posting has separate application tests. The final close clicks the
application's titlebar Quit action, so it also checks input after recovery without
requiring a window manager.

The runner creates an owned, authenticated Xvfb display, private configuration,
data, state, cache and runtime directories, and a private session bus when
available. It clears desktop/compositor connection variables and uses dummy
audio; it never opens the user's desktop window. It withholds acquisition,
holds presentation-fence readiness across resize, injects out-of-date and lost
surface results, and checks recovery after each fault. The shim rejects a
nonzero acquire timeout, device-idle during an active fault, or a present without
a presentation fence. Repeated polls demonstrate main-loop progress; input,
audio and watchdog behavior also have separate application tests. Logs and
`result.json` are kept for review. A successful source-only test is not evidence
that the real GPU integration test ran.

## Apple behavior and verification limit

Metal's existing `SDL_WaitAndAcquireGPUSwapchainTexture` path first waits for
an in-flight command-buffer fence using `waitUntilCompleted`, which has no
application timeout. This patch does not bound that preceding GPU wait. Metal
then uses `nextDrawable`. Apple's
[timeout documentation](https://developer.apple.com/documentation/quartzcore/cametallayer/allowsnextdrawabletimeout)
describes a one-second timeout; this is not a nonblocking acquire API. The
Apple-only generated source explicitly enables that timeout on supported OS
versions. If acquisition returns nil, it leaves the output texture null and
skips registration of a drawable for presentation. It never calls
`presentDrawable:nil`. SDL's Metal backend, device choice and VSync behavior
remain intact. The one-second drawable timeout therefore does not establish a
bounded overall Metal presentation call; the bounded Linux Vulkan fault results
do not apply to Apple.

The source-immutability/idempotence test also covers this Metal guard. Compiling
and exercising it under withheld drawables requires an Apple SDK and native Mac
runtime; neither is established by the Linux fault run. Manual Apple verification
should cover a nil drawable after timeout, subsequent successful acquisition,
resize, hide/expose and both supported presentation modes.
