// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/init/gl_factory.h"

#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_context_egl.h"
#include "ui/gl/gl_context_osmesa.h"
#include "ui/gl/gl_context_stub.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/gl_surface_osmesa.h"
#include "ui/gl/gl_surface_stub.h"
#include "ui/gl/init/gl_surface_ozone.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/surface_factory_ozone.h"
#include "ui/ozone/public/surface_ozone_egl.h"

namespace gl {
namespace init {

namespace {

ui::SurfaceFactoryOzone* GetSurfaceFactory() {
  return ui::OzonePlatform::GetInstance()->GetSurfaceFactoryOzone();
}

bool HasDefaultImplementation(GLImplementation impl) {
  return impl == kGLImplementationOSMesaGL || impl == kGLImplementationMockGL;
}

scoped_refptr<GLSurface> CreateDefaultViewGLSurface(
    gfx::AcceleratedWidget window) {
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL:
      return InitializeGLSurface(new GLSurfaceOSMesaHeadless());
    case kGLImplementationMockGL:
      return InitializeGLSurface(new GLSurfaceStub());
    default:
      NOTREACHED();
  }
  return nullptr;
}

scoped_refptr<GLSurface> CreateDefaultOffscreenGLSurface(
    const gfx::Size& size) {
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL:
      return InitializeGLSurface(
          new GLSurfaceOSMesa(GLSurface::SURFACE_OSMESA_BGRA, size));
    case kGLImplementationMockGL:
      return InitializeGLSurface(new GLSurfaceStub);
    default:
      NOTREACHED();
  }
  return nullptr;
}

// TODO(kylechar): Remove when all implementations are switched over.
scoped_refptr<GLSurface> CreateViewGLSurfaceOld(gfx::AcceleratedWidget window) {
  switch (GetGLImplementation()) {
    case kGLImplementationEGLGLES2:
      DCHECK_NE(window, gfx::kNullAcceleratedWidget);
      return CreateViewGLSurfaceOzone(window);
    default:
      NOTREACHED();
  }
  return nullptr;
}

// TODO(kylechar): Remove when all implementations are switched over.
scoped_refptr<GLSurface> CreateOffscreenGLSurfaceOld(const gfx::Size& size) {
  switch (GetGLImplementation()) {
    case kGLImplementationEGLGLES2:
      if (GLSurfaceEGL::IsEGLSurfacelessContextSupported() &&
          (size.width() == 0 && size.height() == 0)) {
        return InitializeGLSurface(new SurfacelessEGL(size));
      } else {
        return InitializeGLSurface(new PbufferGLSurfaceEGL(size));
      }
    default:
      NOTREACHED();
  }
  return nullptr;
}

}  // namespace

scoped_refptr<GLContext> CreateGLContext(GLShareGroup* share_group,
                                         GLSurface* compatible_surface,
                                         GpuPreference gpu_preference) {
  TRACE_EVENT0("gpu", "gl::init::CreateGLContext");
  switch (GetGLImplementation()) {
    case kGLImplementationMockGL:
      return scoped_refptr<GLContext>(new GLContextStub(share_group));
    case kGLImplementationOSMesaGL:
      return InitializeGLContext(new GLContextOSMesa(share_group),
                                 compatible_surface, gpu_preference);
    case kGLImplementationEGLGLES2:
      return InitializeGLContext(new GLContextEGL(share_group),
                                 compatible_surface, gpu_preference);

    default:
      NOTREACHED();
      return nullptr;
  }
}

scoped_refptr<GLSurface> CreateViewGLSurface(gfx::AcceleratedWidget window) {
  TRACE_EVENT0("gpu", "gl::init::CreateViewGLSurface");

  if (HasDefaultImplementation(GetGLImplementation()))
    return CreateDefaultViewGLSurface(window);

  // TODO(kylechar): This is deprecated, remove when possible.
  if (!GetSurfaceFactory()->UseNewSurfaceAPI())
    return CreateViewGLSurfaceOld(window);

  return GetSurfaceFactory()->CreateViewGLSurface(GetGLImplementation(),
                                                  window);
}

scoped_refptr<GLSurface> CreateSurfacelessViewGLSurface(
    gfx::AcceleratedWidget window) {
  TRACE_EVENT0("gpu", "gl::init::CreateSurfacelessViewGLSurface");

  return GetSurfaceFactory()->CreateSurfacelessViewGLSurface(
      GetGLImplementation(), window);
}

scoped_refptr<GLSurface> CreateOffscreenGLSurface(const gfx::Size& size) {
  TRACE_EVENT0("gpu", "gl::init::CreateOffscreenGLSurface");

  if (HasDefaultImplementation(GetGLImplementation()))
    return CreateDefaultOffscreenGLSurface(size);

  // TODO(kylechar): This is deprecated, remove when possible.
  if (!GetSurfaceFactory()->UseNewSurfaceAPI())
    return CreateOffscreenGLSurfaceOld(size);

  return GetSurfaceFactory()->CreateOffscreenGLSurface(GetGLImplementation(),
                                                       size);
}

}  // namespace init
}  // namespace gl
