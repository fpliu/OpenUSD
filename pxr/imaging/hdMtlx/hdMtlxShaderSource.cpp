//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdMtlx/hdMtlxShaderSource.h"

#include "pxr/imaging/hd/material.h"

#include <MaterialXCore/Definition.h>
#include <MaterialXCore/Geom.h>
#include <MaterialXCore/Interface.h>
#include <MaterialXCore/Node.h>
#include <MaterialXCore/Unit.h>

namespace mx = MaterialX;

PXR_NAMESPACE_OPEN_SCOPE

// --- helpers ------------------------------------------------------------------

static bool
_UsesTexcoordNode(const mx::NodeDefPtr& nodeDef)
{
    mx::InterfaceElementPtr impl = nodeDef->getImplementation();
    if (impl && impl->isA<mx::NodeGraph>()) {
        mx::NodeGraphPtr ng = impl->asA<mx::NodeGraph>();
        return !ng->getNodes("texcoord").empty();
    }
    return false;
}

// Helper: delegate port queries on tag-00 (mx::Element) handles to MX API.
static mx::string
_mxPortValueStr(const mx::Element* e)
{
    if (!e) return mx::EMPTY_STRING;
    mx::ConstValueElementPtr ve = e->asA<mx::ValueElement>();
    if (!ve) return mx::EMPTY_STRING;
    // Follow interfacename binding if present.
    mx::ConstInputPtr inp = ve->asA<mx::Input>();
    if (inp && inp->hasInterfaceName()) {
        mx::ConstElementPtr parent = inp->getParent();
        if (parent) {
            mx::ConstInputPtr ifaceInp =
                parent->getChild(inp->getInterfaceName())->asA<mx::Input>();
            if (ifaceInp && ifaceInp->hasValue()) {
                return ifaceInp->getValueString();
            }
        }
    }
    return ve->hasValue() ? ve->getValueString() : mx::EMPTY_STRING;
}

// --- Constructor --------------------------------------------------------------

HdMtlxShaderSource::HdMtlxShaderSource(
    const HdMaterialNetwork2& network,
    const SdfPath& terminalNodePath,
    HdMtlxTexturePrimvarData* mxHdData)
    : _network(network)
    , _terminalPath(terminalNodePath)
    , _rootEntry(nullptr)
{
    _nodeEntries.reserve(network.nodes.size());
    for (const auto& kv : network.nodes) {
        NodeEntry entry;
        entry.path = &kv.first;
        entry.node = &kv.second;
        const size_t idx = _nodeEntries.size();
        _nodeEntries.push_back(std::move(entry));
        _pathToIndex[kv.first] = idx;
    }

    auto rootIt = _pathToIndex.find(terminalNodePath);
    if (rootIt != _pathToIndex.end()) {
        _rootEntry = &_nodeEntries[rootIt->second];
    }

    if (!mxHdData) return;

    // Populate mxHdData by inspecting each node's NodeDef.
    for (const auto& kv : network.nodes) {
        const SdfPath& nodePath = kv.first;
        const HdMaterialNode2& hdNode = kv.second;

        mx::NodeDefPtr nodeDef = HdMtlxGetNodeDef(hdNode.nodeTypeId);
        if (!nodeDef) continue;

        const std::string mxNodeName = HdMtlxCreateNameFromPath(nodePath);
        const std::string nodeCategory = nodeDef->getNodeString();

        // Texture nodes: any NodeDef input of type "filename".
        for (const mx::InputPtr& input : nodeDef->getActiveInputs()) {
            if (input->getType() == "filename") {
                mxHdData->mxHdTextureMap[mxNodeName].insert(input->getName());
                mxHdData->hdTextureNodes.insert(nodePath);
            }
        }

        // Primvar nodes: geompropvalue, texcoord, or custom nodes using texcoord.
        if (nodeCategory == "geompropvalue" || nodeCategory == "texcoord" ||
            _UsesTexcoordNode(nodeDef)) {
            mxHdData->hdPrimvarNodes.insert(nodePath);
        }
    }
}

// --- _buildPorts --------------------------------------------------------------

void
HdMtlxShaderSource::_buildPorts(const NodeEntry& entry) const
{
    if (entry.portsBuilt) return;

    const HdMaterialNode2* hdNode = entry.node;

    // Collect names present in parameters OR inputConnections.
    std::set<TfToken> activeNames;
    for (const auto& kv : hdNode->parameters)
        activeNames.insert(kv.first);
    for (const auto& kv : hdNode->inputConnections)
        activeNames.insert(kv.first);

    mx::NodeDefPtr nodeDef = HdMtlxGetNodeDef(hdNode->nodeTypeId);

    entry.ports.reserve(activeNames.size());
    for (const TfToken& name : activeNames) {
        PortDesc pd;
        pd.name  = name.GetString();
        pd.owner = &entry;

        if (nodeDef) {
            mx::InputPtr ndInp = nodeDef->getActiveInput(pd.name);
            if (ndInp) pd.typeName = ndInp->getType();
        }

        auto paramIt = hdNode->parameters.find(name);
        if (paramIt != hdNode->parameters.end()) {
            pd.hasValue = true;
            pd.valueStr = HdMtlxConvertToString(paramIt->second);
        }

        auto connIt = hdNode->inputConnections.find(name);
        if (connIt != hdNode->inputConnections.end() &&
            !connIt->second.empty()) {
            pd.hasConnection    = true;
            const HdMaterialConnection2& conn = connIt->second[0];
            pd.connectedPath    = conn.upstreamNode;
            pd.connectedOutput  = conn.upstreamOutputName.GetString();
        }

        entry.ports.push_back(std::move(pd));
    }
    entry.portsBuilt = true;
}

// --- _findNodeEntry -----------------------------------------------------------

const HdMtlxShaderSource::NodeEntry*
HdMtlxShaderSource::_findNodeEntry(const SdfPath& path) const
{
    auto it = _pathToIndex.find(path);
    return (it != _pathToIndex.end()) ? &_nodeEntries[it->second] : nullptr;
}

// --- Root ---------------------------------------------------------------------

mx::DataHandle
HdMtlxShaderSource::getRootElement() const
{
    return _rootEntry ? _makeNodeHandle(_rootEntry) : mx::InvalidHandle;
}

// --- Element classification ---------------------------------------------------

bool HdMtlxShaderSource::isNode(mx::DataHandle elem) const
{
    // MX handles: delegate. Node handles: always true. Port handles: false.
    if (_isMxHandle(elem)) {
        const mx::Element* e = _asMx(elem);
        return e && e->isA<mx::Node>();
    }
    return _isNodeHandle(elem);
}

bool HdMtlxShaderSource::isOutput(mx::DataHandle elem) const
{
    if (_isMxHandle(elem)) {
        const mx::Element* e = _asMx(elem);
        return e && e->isA<mx::Output>();
    }
    return false; // Hd nodes and ports are never Output elements
}

bool HdMtlxShaderSource::isNodeGraph(mx::DataHandle elem) const
{
    if (_isMxHandle(elem)) {
        const mx::Element* e = _asMx(elem);
        return e && e->isA<mx::NodeGraph>();
    }
    return false; // flat Hd network has no NodeGraphs
}

// --- Element identity ---------------------------------------------------------

mx::string
HdMtlxShaderSource::getElementName(mx::DataHandle elem) const
{
    if (_isMxHandle(elem)) {
        const mx::Element* e = _asMx(elem);
        return e ? e->getName() : mx::EMPTY_STRING;
    }
    if (_isNodeHandle(elem)) {
        const NodeEntry* ne = _asNodeEntry(elem);
        return ne ? ne->path->GetName() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getElementPath(mx::DataHandle elem) const
{
    if (_isMxHandle(elem)) {
        const mx::Element* e = _asMx(elem);
        return e ? e->getNamePath() : mx::EMPTY_STRING;
    }
    if (_isNodeHandle(elem)) {
        const NodeEntry* ne = _asNodeEntry(elem);
        return ne ? ne->path->GetString() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

// --- Node topology ------------------------------------------------------------

size_t
HdMtlxShaderSource::getNodeInputCount(mx::DataHandle node) const
{
    if (_isMxHandle(node)) {
        const mx::Element* e = _asMx(node);
        mx::ConstNodePtr n = e ? e->asA<mx::Node>() : nullptr;
        return n ? n->getActiveInputs().size() : 0;
    }
    if (_isNodeHandle(node)) {
        const NodeEntry* ne = _asNodeEntry(node);
        if (!ne) return 0;
        _buildPorts(*ne);
        return ne->ports.size();
    }
    return 0;
}

mx::DataHandle
HdMtlxShaderSource::getNodeInput(mx::DataHandle node, size_t index) const
{
    if (_isMxHandle(node)) {
        const mx::Element* e = _asMx(node);
        mx::ConstNodePtr n = e ? e->asA<mx::Node>() : nullptr;
        if (!n) return mx::InvalidHandle;
        const auto inputs = n->getActiveInputs();
        return (index < inputs.size())
            ? _makeMxHandle(inputs[index].get()) : mx::InvalidHandle;
    }
    if (_isNodeHandle(node)) {
        const NodeEntry* ne = _asNodeEntry(node);
        if (!ne) return mx::InvalidHandle;
        _buildPorts(*ne);
        return (index < ne->ports.size())
            ? _makePortHandle(&ne->ports[index]) : mx::InvalidHandle;
    }
    return mx::InvalidHandle;
}

mx::DataHandle
HdMtlxShaderSource::getNodeInputByName(
    mx::DataHandle node, const mx::string& name) const
{
    if (_isMxHandle(node)) {
        const mx::Element* e = _asMx(node);
        mx::ConstNodePtr n = e ? e->asA<mx::Node>() : nullptr;
        return n ? _makeMxHandle(n->getInput(name).get()) : mx::InvalidHandle;
    }
    if (_isNodeHandle(node)) {
        const NodeEntry* ne = _asNodeEntry(node);
        if (!ne) return mx::InvalidHandle;
        _buildPorts(*ne);
        for (const PortDesc& pd : ne->ports) {
            if (pd.name == name) return _makePortHandle(&pd);
        }
        return mx::InvalidHandle;
    }
    return mx::InvalidHandle;
}

mx::DataHandle
HdMtlxShaderSource::getInputConnectedNode(mx::DataHandle input) const
{
    if (_isMxHandle(input)) {
        const mx::Element* e = _asMx(input);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp ? _makeMxHandle(inp->getConnectedNode().get())
                   : mx::InvalidHandle;
    }
    if (_isPortHandle(input)) {
        const PortDesc* pd = _asPortDesc(input);
        if (!pd || !pd->hasConnection) return mx::InvalidHandle;
        const NodeEntry* upstream = _findNodeEntry(pd->connectedPath);
        return upstream ? _makeNodeHandle(upstream) : mx::InvalidHandle;
    }
    return mx::InvalidHandle;
}

mx::string
HdMtlxShaderSource::getInputConnectedOutputName(mx::DataHandle input) const
{
    if (_isMxHandle(input)) {
        const mx::Element* e = _asMx(input);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp ? inp->getOutputString() : mx::EMPTY_STRING;
    }
    if (_isPortHandle(input)) {
        const PortDesc* pd = _asPortDesc(input);
        return (pd && pd->hasConnection) ? pd->connectedOutput
                                         : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::DataHandle
HdMtlxShaderSource::getOutputConnectedNode(mx::DataHandle output) const
{
    if (_isMxHandle(output)) {
        const mx::Element* e = _asMx(output);
        mx::ConstOutputPtr out = e ? e->asA<mx::Output>() : nullptr;
        return out ? _makeMxHandle(out->getConnectedNode().get())
                   : mx::InvalidHandle;
    }
    return mx::InvalidHandle; // flat Hd has no Output elements
}

mx::DataHandle
HdMtlxShaderSource::getNodeParentGraph(mx::DataHandle node) const
{
    if (_isMxHandle(node)) {
        const mx::Element* e = _asMx(node);
        if (!e) return mx::InvalidHandle;
        mx::ConstElementPtr parent = e->getParent();
        return (parent && parent->isA<mx::NodeGraph>())
            ? _makeMxHandle(parent.get()) : mx::InvalidHandle;
    }
    return mx::InvalidHandle; // all Hd nodes live at the top level
}

// --- Node definition lookup ---------------------------------------------------

mx::string
HdMtlxShaderSource::getNodeDefName(mx::DataHandle node) const
{
    if (_isMxHandle(node)) {
        const mx::Element* e = _asMx(node);
        mx::ConstNodePtr n = e ? e->asA<mx::Node>() : nullptr;
        if (!n) return mx::EMPTY_STRING;
        mx::ConstNodeDefPtr nd = n->getNodeDef();
        return nd ? nd->getName() : mx::EMPTY_STRING;
    }
    if (_isNodeHandle(node)) {
        const NodeEntry* ne = _asNodeEntry(node);
        if (!ne) return mx::EMPTY_STRING;
        mx::NodeDefPtr nd = HdMtlxGetNodeDef(ne->node->nodeTypeId);
        return nd ? nd->getName() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::DataHandle
HdMtlxShaderSource::getNodeDef(mx::DataHandle node) const
{
    if (_isMxHandle(node)) {
        const mx::Element* e = _asMx(node);
        mx::ConstNodePtr n = e ? e->asA<mx::Node>() : nullptr;
        return n ? _makeMxHandle(n->getNodeDef().get()) : mx::InvalidHandle;
    }
    if (_isNodeHandle(node)) {
        const NodeEntry* ne = _asNodeEntry(node);
        if (!ne) return mx::InvalidHandle;
        mx::NodeDefPtr nd = HdMtlxGetNodeDef(ne->node->nodeTypeId);
        return _makeMxHandle(nd.get());
    }
    return mx::InvalidHandle;
}

mx::DataHandle
HdMtlxShaderSource::getNodeDefByName(const mx::string& nodeDefName) const
{
    const mx::ConstDocumentPtr& libs = HdMtlxStdLibraries();
    return _makeMxHandle(libs->getNodeDef(nodeDefName).get());
}

// --- NodeDef interface --------------------------------------------------------

mx::string
HdMtlxShaderSource::getNodeDefType(mx::DataHandle nodeDef) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    return nd ? nd->getType() : mx::EMPTY_STRING;
}

size_t
HdMtlxShaderSource::getNodeDefInputCount(mx::DataHandle nodeDef) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    return nd ? nd->getActiveInputs().size() : 0;
}

mx::DataHandle
HdMtlxShaderSource::getNodeDefInput(
    mx::DataHandle nodeDef, size_t index) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    if (!nd) return mx::InvalidHandle;
    const auto inputs = nd->getActiveInputs();
    return (index < inputs.size())
        ? _makeMxHandle(inputs[index].get()) : mx::InvalidHandle;
}

mx::DataHandle
HdMtlxShaderSource::getNodeDefInputByName(
    mx::DataHandle nodeDef, const mx::string& name) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    return nd ? _makeMxHandle(nd->getInput(name).get()) : mx::InvalidHandle;
}

size_t
HdMtlxShaderSource::getNodeDefOutputCount(mx::DataHandle nodeDef) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    return nd ? nd->getActiveOutputs().size() : 0;
}

mx::DataHandle
HdMtlxShaderSource::getNodeDefOutput(
    mx::DataHandle nodeDef, size_t index) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    if (!nd) return mx::InvalidHandle;
    const auto outputs = nd->getActiveOutputs();
    return (index < outputs.size())
        ? _makeMxHandle(outputs[index].get()) : mx::InvalidHandle;
}

mx::DataHandle
HdMtlxShaderSource::getNodeDefOutputByName(
    mx::DataHandle nodeDef, const mx::string& name) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    return nd ? _makeMxHandle(nd->getOutput(name).get()) : mx::InvalidHandle;
}

size_t
HdMtlxShaderSource::getNodeDefValueElementCount(mx::DataHandle nodeDef) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    return nd ? nd->getActiveValueElements().size() : 0;
}

mx::DataHandle
HdMtlxShaderSource::getNodeDefValueElement(
    mx::DataHandle nodeDef, size_t index) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    mx::ConstNodeDefPtr nd = e ? e->asA<mx::NodeDef>() : nullptr;
    if (!nd) return mx::InvalidHandle;
    const auto elems = nd->getActiveValueElements();
    return (index < elems.size())
        ? _makeMxHandle(elems[index].get()) : mx::InvalidHandle;
}

bool
HdMtlxShaderSource::valueElementIsOutput(mx::DataHandle valueElem) const
{
    const mx::Element* e = _isMxHandle(valueElem) ? _asMx(valueElem) : nullptr;
    return e && e->isA<mx::Output>();
}

mx::string
HdMtlxShaderSource::getNodeDefAttribute(
    mx::DataHandle nodeDef, const mx::string& attrName) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    return e ? e->getAttribute(attrName) : mx::EMPTY_STRING;
}

void
HdMtlxShaderSource::getNodeDefAttributeNames(
    mx::DataHandle nodeDef, mx::StringVec& names) const
{
    const mx::Element* e = _isMxHandle(nodeDef) ? _asMx(nodeDef) : nullptr;
    if (e) names = e->getAttributeNames();
}

// --- NodeGraph interface (N/A for flat Hd networks) ---------------------------

mx::DataHandle
HdMtlxShaderSource::getNodeGraphNodeDef(mx::DataHandle nodeGraph) const
{
    if (_isMxHandle(nodeGraph)) {
        const mx::Element* e = _asMx(nodeGraph);
        mx::ConstNodeGraphPtr ng = e ? e->asA<mx::NodeGraph>() : nullptr;
        return ng ? _makeMxHandle(ng->getNodeDef().get()) : mx::InvalidHandle;
    }
    return mx::InvalidHandle;
}

mx::string
HdMtlxShaderSource::getNodeGraphName(mx::DataHandle nodeGraph) const
{
    if (_isMxHandle(nodeGraph)) {
        const mx::Element* e = _asMx(nodeGraph);
        mx::ConstNodeGraphPtr ng = e ? e->asA<mx::NodeGraph>() : nullptr;
        return ng ? ng->getName() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

size_t
HdMtlxShaderSource::getNodeGraphInputCount(mx::DataHandle nodeGraph) const
{
    if (_isMxHandle(nodeGraph)) {
        const mx::Element* e = _asMx(nodeGraph);
        mx::ConstNodeGraphPtr ng = e ? e->asA<mx::NodeGraph>() : nullptr;
        return ng ? ng->getActiveInputs().size() : 0;
    }
    return 0;
}

mx::DataHandle
HdMtlxShaderSource::getNodeGraphInput(
    mx::DataHandle nodeGraph, size_t index) const
{
    if (_isMxHandle(nodeGraph)) {
        const mx::Element* e = _asMx(nodeGraph);
        mx::ConstNodeGraphPtr ng = e ? e->asA<mx::NodeGraph>() : nullptr;
        if (!ng) return mx::InvalidHandle;
        const auto inputs = ng->getActiveInputs();
        return (index < inputs.size())
            ? _makeMxHandle(inputs[index].get()) : mx::InvalidHandle;
    }
    return mx::InvalidHandle;
}

mx::DataHandle
HdMtlxShaderSource::getOutputParentNodeGraph(mx::DataHandle output) const
{
    if (_isMxHandle(output)) {
        const mx::Element* e = _asMx(output);
        if (!e) return mx::InvalidHandle;
        mx::ConstElementPtr parent = e->getParent();
        return (parent && parent->isA<mx::NodeGraph>())
            ? _makeMxHandle(parent.get()) : mx::InvalidHandle;
    }
    return mx::InvalidHandle;
}

// --- Port queries -------------------------------------------------------------

mx::string
HdMtlxShaderSource::getPortName(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        return e ? e->getName() : mx::EMPTY_STRING;
    }
    if (_isPortHandle(port)) {
        const PortDesc* pd = _asPortDesc(port);
        return pd ? pd->name : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getPortType(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstValueElementPtr ve = e ? e->asA<mx::ValueElement>() : nullptr;
        return ve ? ve->getType() : mx::EMPTY_STRING;
    }
    if (_isPortHandle(port)) {
        const PortDesc* pd = _asPortDesc(port);
        return pd ? pd->typeName : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getPortPath(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        return e ? e->getNamePath() : mx::EMPTY_STRING;
    }
    if (_isPortHandle(port)) {
        const PortDesc* pd = _asPortDesc(port);
        if (!pd || !pd->owner) return mx::EMPTY_STRING;
        return pd->owner->path->GetString() + "/" + pd->name;
    }
    return mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getPortValueString(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        return _mxPortValueStr(_asMx(port));
    }
    if (_isPortHandle(port)) {
        const PortDesc* pd = _asPortDesc(port);
        return (pd && pd->hasValue) ? pd->valueStr : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

bool
HdMtlxShaderSource::portHasValue(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstValueElementPtr ve = e ? e->asA<mx::ValueElement>() : nullptr;
        return ve && ve->hasValue();
    }
    if (_isPortHandle(port)) {
        const PortDesc* pd = _asPortDesc(port);
        return pd && pd->hasValue;
    }
    return false;
}

mx::string
HdMtlxShaderSource::getPortAttribute(
    mx::DataHandle port, const mx::string& attrName) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        return e ? e->getAttribute(attrName) : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING; // Hd ports carry no XML attributes
}

void
HdMtlxShaderSource::getPortAttributeNames(
    mx::DataHandle port, mx::StringVec& names) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        if (e) names = e->getAttributeNames();
    }
    // Hd ports have no XML attributes; leave names empty.
}

// --- Interface binding (N/A) --------------------------------------------------

bool HdMtlxShaderSource::portHasInterfaceName(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp && inp->hasInterfaceName();
    }
    return false;
}

mx::string
HdMtlxShaderSource::getPortInterfaceName(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return (inp && inp->hasInterfaceName()) ? inp->getInterfaceName()
                                                : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::DataHandle
HdMtlxShaderSource::getPortInterfaceInput(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        if (!inp || !inp->hasInterfaceName()) return mx::InvalidHandle;
        mx::ConstElementPtr parent = inp->getParent();
        if (!parent) return mx::InvalidHandle;
        return _makeMxHandle(
            parent->getChild(inp->getInterfaceName()).get());
    }
    return mx::InvalidHandle;
}

// --- Geometric default property -----------------------------------------------

bool
HdMtlxShaderSource::portHasDefaultGeomProp(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp && inp->getDefaultGeomProp() != nullptr;
    }
    return false; // instance-level ports don't carry defaultgeomprop
}

mx::DataHandle
HdMtlxShaderSource::getPortDefaultGeomProp(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp ? _makeMxHandle(inp->getDefaultGeomProp().get())
                   : mx::InvalidHandle;
    }
    return mx::InvalidHandle;
}

// --- Unit metadata ------------------------------------------------------------

mx::string HdMtlxShaderSource::getPortUnit(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstValueElementPtr ve = e ? e->asA<mx::ValueElement>() : nullptr;
        return ve ? ve->getUnit() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::string HdMtlxShaderSource::getPortUnitType(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstValueElementPtr ve = e ? e->asA<mx::ValueElement>() : nullptr;
        return ve ? ve->getUnitType() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::string HdMtlxShaderSource::getPortActiveUnit(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp ? inp->getActiveUnit() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

// --- Color space metadata -----------------------------------------------------

mx::string HdMtlxShaderSource::getPortColorSpace(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstValueElementPtr ve = e ? e->asA<mx::ValueElement>() : nullptr;
        return ve ? ve->getColorSpace() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getPortActiveColorSpace(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp ? inp->getActiveColorSpace() : mx::EMPTY_STRING;
    }
    return mx::EMPTY_STRING;
}

// --- Uniformity ---------------------------------------------------------------

bool HdMtlxShaderSource::portIsUniform(mx::DataHandle port) const
{
    if (_isMxHandle(port)) {
        const mx::Element* e = _asMx(port);
        mx::ConstInputPtr inp = e ? e->asA<mx::Input>() : nullptr;
        return inp && inp->getIsUniform();
    }
    return false;
}

// --- GeomPropDef queries ------------------------------------------------------

mx::string
HdMtlxShaderSource::getGeomPropDefName(mx::DataHandle geomPropDef) const
{
    const mx::Element* e = _isMxHandle(geomPropDef) ? _asMx(geomPropDef)
                                                     : nullptr;
    mx::ConstGeomPropDefPtr gp = e ? e->asA<mx::GeomPropDef>() : nullptr;
    return gp ? gp->getName() : mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getGeomPropDefProp(mx::DataHandle geomPropDef) const
{
    const mx::Element* e = _isMxHandle(geomPropDef) ? _asMx(geomPropDef)
                                                     : nullptr;
    mx::ConstGeomPropDefPtr gp = e ? e->asA<mx::GeomPropDef>() : nullptr;
    return gp ? gp->getGeomProp() : mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getGeomPropDefPath(mx::DataHandle geomPropDef) const
{
    const mx::Element* e = _isMxHandle(geomPropDef) ? _asMx(geomPropDef)
                                                     : nullptr;
    return e ? e->getNamePath() : mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getGeomPropDefSpace(mx::DataHandle geomPropDef) const
{
    const mx::Element* e = _isMxHandle(geomPropDef) ? _asMx(geomPropDef)
                                                     : nullptr;
    mx::ConstGeomPropDefPtr gp = e ? e->asA<mx::GeomPropDef>() : nullptr;
    return gp ? gp->getSpace() : mx::EMPTY_STRING;
}

mx::string
HdMtlxShaderSource::getGeomPropDefIndex(mx::DataHandle geomPropDef) const
{
    const mx::Element* e = _isMxHandle(geomPropDef) ? _asMx(geomPropDef)
                                                     : nullptr;
    mx::ConstGeomPropDefPtr gp = e ? e->asA<mx::GeomPropDef>() : nullptr;
    return gp ? gp->getIndex() : mx::EMPTY_STRING;
}

// --- Document-level queries ---------------------------------------------------

mx::string HdMtlxShaderSource::getActiveColorSpace() const
{
    return mx::EMPTY_STRING; // no document-level color space in Hd networks
}

mx::DataHandle
HdMtlxShaderSource::getUnitTypeDefByName(const mx::string& unitTypeName) const
{
    const mx::ConstDocumentPtr& libs = HdMtlxStdLibraries();
    return _makeMxHandle(libs->getUnitTypeDef(unitTypeName).get());
}

// --- MX compatibility bridge --------------------------------------------------

mx::ConstDocumentPtr HdMtlxShaderSource::getMxDocument() const
{
    // Return stdlib so HwShaderGenerator::createShader(ShaderGraphPtr) can
    // look up GeomPropDefs for the socket geomProp insertion pass.
    return HdMtlxStdLibraries();
}

mx::ConstNodeDefPtr
HdMtlxShaderSource::getMxNodeDef(mx::DataHandle node) const
{
    if (_isNodeHandle(node)) {
        const NodeEntry* ne = _asNodeEntry(node);
        return ne ? HdMtlxGetNodeDef(ne->node->nodeTypeId) : nullptr;
    }
    if (_isMxHandle(node)) {
        // Called with a NodeDef handle by mistake — cast it.
        const mx::Element* e = _asMx(node);
        mx::ConstNodePtr n = e ? e->asA<mx::Node>() : nullptr;
        return n ? n->getNodeDef() : nullptr;
    }
    return nullptr;
}

mx::ConstNodeDefPtr
HdMtlxShaderSource::getMxNodeDefByHandle(mx::DataHandle nodeDefHandle) const
{
    if (!_isMxHandle(nodeDefHandle)) return nullptr;
    const mx::Element* e = _asMx(nodeDefHandle);
    if (!e) return nullptr;
    // e lives in HdMtlxStdLibraries() which is a static; getSelf() returns the
    // existing shared_ptr without transferring ownership.
    return std::dynamic_pointer_cast<const mx::NodeDef>(
        const_cast<mx::Element*>(e)->getSelf());
}

PXR_NAMESPACE_CLOSE_SCOPE
