//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef PXR_IMAGING_HD_MTLX_SHADER_SOURCE_H
#define PXR_IMAGING_HD_MTLX_SHADER_SOURCE_H

#include "pxr/pxr.h"
#include "pxr/imaging/hdMtlx/api.h"
#include "pxr/imaging/hdMtlx/hdMtlx.h"
#include "pxr/usd/sdf/path.h"

#include <MaterialXGenShader2/IShaderSource.h>

#include <map>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

struct HdMaterialNetwork2;
struct HdMaterialNode2;

/// \class HdMtlxShaderSource
///
/// IShaderSource implementation backed by a Hydra HdMaterialNetwork2.
///
/// Wraps a flat Hydra material network (no NodeGraph hierarchy, no
/// interfacename bindings) so that MaterialXGenShader2's ShaderGraphBuilder
/// can drive shader generation without constructing a full mx::Document.
///
/// Handle encoding (low 2 bits as type tag):
///   0b00 — mx::Element* from the MaterialX standard library
///   0b01 — NodeEntry*   (a node instance in the Hd network)
///   0b10 — PortDesc*    (an input port on a node instance)
///
class HDMTLX_API HdMtlxShaderSource : public MaterialX::IShaderSource
{
public:
    /// Construct from a Hydra material network and the SdfPath of the
    /// terminal (surface shader) node.  If \p mxHdData is non-null it is
    /// populated with texture and primvar node information.
    HdMtlxShaderSource(
        const HdMaterialNetwork2& network,
        const SdfPath& terminalNodePath,
        HdMtlxTexturePrimvarData* mxHdData = nullptr);

    ~HdMtlxShaderSource() override = default;

    // --- Root -----------------------------------------------------------------
    MaterialX::DataHandle getRootElement() const override;

    // --- Element classification -----------------------------------------------
    bool isNode(MaterialX::DataHandle elem) const override;
    bool isOutput(MaterialX::DataHandle elem) const override;
    bool isNodeGraph(MaterialX::DataHandle elem) const override;

    // --- Element identity -----------------------------------------------------
    MaterialX::string getElementName(MaterialX::DataHandle elem) const override;
    MaterialX::string getElementPath(MaterialX::DataHandle elem) const override;

    // --- Node topology --------------------------------------------------------
    size_t getNodeInputCount(MaterialX::DataHandle node) const override;
    MaterialX::DataHandle getNodeInput(
        MaterialX::DataHandle node, size_t index) const override;
    MaterialX::DataHandle getNodeInputByName(
        MaterialX::DataHandle node,
        const MaterialX::string& name) const override;
    MaterialX::DataHandle getInputConnectedNode(
        MaterialX::DataHandle input) const override;
    MaterialX::string getInputConnectedOutputName(
        MaterialX::DataHandle input) const override;
    MaterialX::DataHandle getOutputConnectedNode(
        MaterialX::DataHandle output) const override;
    MaterialX::DataHandle getNodeParentGraph(
        MaterialX::DataHandle node) const override;

    // --- Node definition lookup -----------------------------------------------
    MaterialX::string getNodeDefName(MaterialX::DataHandle node) const override;
    MaterialX::DataHandle getNodeDef(MaterialX::DataHandle node) const override;
    MaterialX::DataHandle getNodeDefByName(
        const MaterialX::string& nodeDefName) const override;

    // --- NodeDef interface ----------------------------------------------------
    MaterialX::string getNodeDefType(
        MaterialX::DataHandle nodeDef) const override;
    size_t getNodeDefInputCount(MaterialX::DataHandle nodeDef) const override;
    MaterialX::DataHandle getNodeDefInput(
        MaterialX::DataHandle nodeDef, size_t index) const override;
    MaterialX::DataHandle getNodeDefInputByName(
        MaterialX::DataHandle nodeDef,
        const MaterialX::string& name) const override;
    size_t getNodeDefOutputCount(MaterialX::DataHandle nodeDef) const override;
    MaterialX::DataHandle getNodeDefOutput(
        MaterialX::DataHandle nodeDef, size_t index) const override;
    MaterialX::DataHandle getNodeDefOutputByName(
        MaterialX::DataHandle nodeDef,
        const MaterialX::string& name) const override;
    size_t getNodeDefValueElementCount(
        MaterialX::DataHandle nodeDef) const override;
    MaterialX::DataHandle getNodeDefValueElement(
        MaterialX::DataHandle nodeDef, size_t index) const override;
    bool valueElementIsOutput(MaterialX::DataHandle valueElem) const override;
    MaterialX::string getNodeDefAttribute(
        MaterialX::DataHandle nodeDef,
        const MaterialX::string& attrName) const override;
    void getNodeDefAttributeNames(
        MaterialX::DataHandle nodeDef,
        MaterialX::StringVec& names) const override;

    // --- NodeGraph interface (N/A for flat Hd networks) ----------------------
    MaterialX::DataHandle getNodeGraphNodeDef(
        MaterialX::DataHandle nodeGraph) const override;
    MaterialX::string getNodeGraphName(
        MaterialX::DataHandle nodeGraph) const override;
    size_t getNodeGraphInputCount(
        MaterialX::DataHandle nodeGraph) const override;
    MaterialX::DataHandle getNodeGraphInput(
        MaterialX::DataHandle nodeGraph, size_t index) const override;
    MaterialX::DataHandle getOutputParentNodeGraph(
        MaterialX::DataHandle output) const override;

    // --- Port queries ---------------------------------------------------------
    MaterialX::string getPortName(MaterialX::DataHandle port) const override;
    MaterialX::string getPortType(MaterialX::DataHandle port) const override;
    MaterialX::string getPortPath(MaterialX::DataHandle port) const override;
    MaterialX::string getPortValueString(
        MaterialX::DataHandle port) const override;
    bool portHasValue(MaterialX::DataHandle port) const override;
    MaterialX::string getPortAttribute(
        MaterialX::DataHandle port,
        const MaterialX::string& attrName) const override;
    void getPortAttributeNames(
        MaterialX::DataHandle port,
        MaterialX::StringVec& names) const override;

    // --- Interface binding (N/A for flat Hd networks) -------------------------
    bool portHasInterfaceName(MaterialX::DataHandle port) const override;
    MaterialX::string getPortInterfaceName(
        MaterialX::DataHandle port) const override;
    MaterialX::DataHandle getPortInterfaceInput(
        MaterialX::DataHandle port) const override;

    // --- Geometric default property -------------------------------------------
    bool portHasDefaultGeomProp(MaterialX::DataHandle port) const override;
    MaterialX::DataHandle getPortDefaultGeomProp(
        MaterialX::DataHandle port) const override;

    // --- Unit metadata --------------------------------------------------------
    MaterialX::string getPortUnit(MaterialX::DataHandle port) const override;
    MaterialX::string getPortUnitType(
        MaterialX::DataHandle port) const override;
    MaterialX::string getPortActiveUnit(
        MaterialX::DataHandle port) const override;

    // --- Color space metadata -------------------------------------------------
    MaterialX::string getPortColorSpace(
        MaterialX::DataHandle port) const override;
    MaterialX::string getPortActiveColorSpace(
        MaterialX::DataHandle port) const override;

    // --- Uniformity -----------------------------------------------------------
    bool portIsUniform(MaterialX::DataHandle port) const override;

    // --- GeomPropDef queries --------------------------------------------------
    MaterialX::string getGeomPropDefName(
        MaterialX::DataHandle geomPropDef) const override;
    MaterialX::string getGeomPropDefProp(
        MaterialX::DataHandle geomPropDef) const override;
    MaterialX::string getGeomPropDefPath(
        MaterialX::DataHandle geomPropDef) const override;
    MaterialX::string getGeomPropDefSpace(
        MaterialX::DataHandle geomPropDef) const override;
    MaterialX::string getGeomPropDefIndex(
        MaterialX::DataHandle geomPropDef) const override;

    // --- Document-level queries -----------------------------------------------
    MaterialX::string getActiveColorSpace() const override;
    MaterialX::DataHandle getUnitTypeDefByName(
        const MaterialX::string& unitTypeName) const override;

    // --- MX compatibility bridge ----------------------------------------------
    /// Returns HdMtlxStdLibraries() so HwShaderGenerator can look up
    /// GeomPropDefs for the socket geomProp insertion pass.
    MaterialX::ConstDocumentPtr getMxDocument() const override;
    MaterialX::ConstNodeDefPtr getMxNodeDef(
        MaterialX::DataHandle node) const override;
    MaterialX::ConstNodeDefPtr getMxNodeDefByHandle(
        MaterialX::DataHandle nodeDefHandle) const override;

private:
    // --- Handle encoding ------------------------------------------------------
    static constexpr uint64_t _TAG_MX   = 0x0;
    static constexpr uint64_t _TAG_NODE = 0x1;
    static constexpr uint64_t _TAG_PORT = 0x2;
    static constexpr uint64_t _TAG_MASK = 0x3;

    struct NodeEntry;
    struct PortDesc;

    static MaterialX::DataHandle _makeNodeHandle(const NodeEntry* e) noexcept {
        return reinterpret_cast<uintptr_t>(e) | _TAG_NODE;
    }
    static MaterialX::DataHandle _makeMxHandle(
        const MaterialX::Element* e) noexcept {
        return e ? (reinterpret_cast<uintptr_t>(e) | _TAG_MX)
                 : MaterialX::InvalidHandle;
    }
    static MaterialX::DataHandle _makePortHandle(
        const PortDesc* p) noexcept {
        return reinterpret_cast<uintptr_t>(p) | _TAG_PORT;
    }
    static bool _isMxHandle(MaterialX::DataHandle h) noexcept {
        return h != MaterialX::InvalidHandle && (h & _TAG_MASK) == _TAG_MX;
    }
    static bool _isNodeHandle(MaterialX::DataHandle h) noexcept {
        return (h & _TAG_MASK) == _TAG_NODE;
    }
    static bool _isPortHandle(MaterialX::DataHandle h) noexcept {
        return (h & _TAG_MASK) == _TAG_PORT;
    }
    static const MaterialX::Element* _asMx(MaterialX::DataHandle h) noexcept {
        return reinterpret_cast<const MaterialX::Element*>(
            static_cast<uintptr_t>(h & ~_TAG_MASK));
    }
    static const NodeEntry* _asNodeEntry(MaterialX::DataHandle h) noexcept {
        return reinterpret_cast<const NodeEntry*>(
            static_cast<uintptr_t>(h & ~_TAG_MASK));
    }
    static const PortDesc* _asPortDesc(MaterialX::DataHandle h) noexcept {
        return reinterpret_cast<const PortDesc*>(
            static_cast<uintptr_t>(h & ~_TAG_MASK));
    }

    // --- Internal data structures ---------------------------------------------

    struct PortDesc
    {
        std::string name;
        std::string typeName;        ///< MX type string from NodeDef input
        bool hasValue      = false;
        std::string valueStr;
        bool hasConnection = false;
        SdfPath connectedPath;
        std::string connectedOutput; ///< empty = default output
        const NodeEntry* owner = nullptr;
    };

    struct NodeEntry
    {
        const SdfPath*         path = nullptr;
        const HdMaterialNode2* node = nullptr;
        mutable std::vector<PortDesc> ports;
        mutable bool portsBuilt = false;
    };

    void _buildPorts(const NodeEntry& entry) const;
    const NodeEntry* _findNodeEntry(const SdfPath& path) const;

    // --- Data -----------------------------------------------------------------
    const HdMaterialNetwork2& _network;
    SdfPath                   _terminalPath;
    const NodeEntry*          _rootEntry;

    mutable std::vector<NodeEntry> _nodeEntries;
    std::map<SdfPath, size_t>      _pathToIndex;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_HD_MTLX_SHADER_SOURCE_H
