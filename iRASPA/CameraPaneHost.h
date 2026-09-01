#pragma once

#include "projectstructure.h"
#include "rkcamera.h"

#include <cstdint>
#include <memory>
#include <string>

// What the camera form needs from the window. The camera and the lights belong to
// the renderer and the project rather than to the document, so this does not go
// through DocumentController: none of it is undoable, and all of it ends in a
// redraw.
struct CameraPaneHost
{
    virtual ~CameraPaneHost() = default;

    // Null before a project is loaded, which the form has to expect: the window
    // comes up with the inspector already on a tab.
    virtual std::shared_ptr<RKCamera> PaneCamera() = 0;
    virtual std::shared_ptr<ProjectStructure> PaneProject() = 0;
    virtual void RedrawRenderer() = 0;
    virtual void ReloadRenderer() = 0;
    // Renderer-side camera commands, which do nothing before the render view is
    // up; Cocoa's are no-ops then too.
    virtual void ResetRendererCameraView() = 0;
    virtual void SetRendererCameraOrthographic(bool orthographic) = 0;
    virtual void ZoomRendererCamera(double amount) = 0;
    // Needs the window handle, so the picker stays with the window.
    virtual void PickBackgroundImage() = 0;
    virtual void Log(std::wstring const& message) = 0;

    // LUID of the adapter the live view draws on, packed into one integer, so the
    // export helper can prefer a second GPU. Zero before the render view is up.
    virtual int64_t LiveAdapterLuid() = 0;

    // Whether the live view's adapter can trace, and the one line saying what it
    // came out as. The Shadows box needs both: a device without DXR 1.1 can never
    // produce a traced shadow, so offering the switch would only mislead.
    virtual bool LiveSupportsRaytracing() = 0;
    virtual std::wstring LiveRaytracingStatus() = 0;
};
