/*
 * Copyright (C) 2013 GREE, Inc.
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 * 
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "cocos2d.h"
#include "lwf_cocos2dx_bitmap.h"
#include "lwf_cocos2dx_factory.h"
#include "lwf_cocos2dx_node.h"
#include "lwf_cocos2dx_particle.h"
#include "lwf_cocos2dx_textbmfont.h"
#include "lwf_cocos2dx_textttf.h"
#include "lwf_core.h"
#include "lwf_data.h"
#include "lwf_property.h"
#include "lwf_text.h"

NS_CC_BEGIN

class LWFMask : public Node
{
private:
	RenderTexture *m_renderTexture;
	BlendFunc m_blendFunc;

public:
	static LWFMask *create() {
		LWFMask *mask = new LWFMask();
		if (mask && mask->init()) {
			mask->autorelease();
			return mask;
		}
		CC_SAFE_DELETE(mask);
		return NULL;
	}

	LWFMask()
		: m_renderTexture(0)
	{
	}

	virtual ~LWFMask()
	{
		CC_SAFE_RELEASE(m_renderTexture);
	}

	virtual bool init() override
	{
		Size size = Director::getInstance()->getVisibleSize();
		m_renderTexture = RenderTexture::create(size.width, size.height);
		if (!m_renderTexture)
			return false;
		m_renderTexture->retain();
		m_renderTexture->setKeepMatrix(true);
		m_renderTexture->setAnchorPoint(Vec2::ZERO);
		m_renderTexture->getSprite()->setAnchorPoint(Vec2::ZERO);
		return Node::init();
	}

	void setBlendFunc(BlendFunc blendFunc)
	{
		m_blendFunc = blendFunc;
	}

	virtual void visit(Renderer *renderer,
		const Mat4& parentTransform, uint32_t parentFlags) override
	{
		m_renderTexture->beginWithClear(0, 0, 0, 0);
		Node::visit(renderer, parentTransform, parentFlags);
		m_renderTexture->end();

		m_renderTexture->getSprite()->setBlendFunc(m_blendFunc);
		m_renderTexture->setLocalZOrder(getLocalZOrder());
		m_renderTexture->visit(renderer, Mat4::IDENTITY, 0);
	}
};

NS_CC_END

USING_NS_CC;

namespace LWF {

static void PlaceNode(Node *newParent, Node *node)
{
	if (newParent && node && node->getParent() != newParent) {
		node->retain();
		node->removeFromParentAndCleanup(false);
		newParent->addChild(node);
		node->release();
	}
}

static GLenum GetBlendDstFactor(int blendMode)
{
	switch (blendMode) {
	case Format::BLEND_MODE_ADD:
		return GL_ONE;

	case Format::BLEND_MODE_MULTIPLY:
		return GL_DST_COLOR;

	case Format::BLEND_MODE_SCREEN:
		return GL_ONE;

	default:
		return GL_ONE_MINUS_SRC_ALPHA;
	}
}

shared_ptr<Renderer> LWFRendererFactory::ConstructBitmap(
	LWF *lwf, int objId, Bitmap *bitmap)
{
	return make_shared<LWFBitmapRenderer>(lwf, bitmap, m_node);
}

shared_ptr<Renderer> LWFRendererFactory::ConstructBitmapEx(
	LWF *lwf, int objId, BitmapEx *bitmapEx)
{
	return make_shared<LWFBitmapRenderer>(lwf, bitmapEx, m_node);
}

shared_ptr<TextRenderer> LWFRendererFactory::ConstructText(
	LWF *lwf, int objId, Text *text)
{
	const Format::Text &t = lwf->data->texts[text->objectId];
	const Format::TextProperty &p = lwf->data->textProperties[t.textPropertyId];
	const Format::Font &f = lwf->data->fonts[p.fontId];
	string fontName = lwf->data->strings[f.stringId];

	if (fontName[0] == '_') {
		fontName = fontName.substr(1);
		return make_shared<LWFTextTTFRenderer>(
			lwf, text, fontName.c_str(), m_node);
	} else {
		return make_shared<LWFTextBMFontRenderer>(
			lwf, text, fontName.c_str(), m_node);
	}
}

shared_ptr<Renderer> LWFRendererFactory::ConstructParticle(
	LWF *lwf, int objId, Particle *particle)
{
	return make_shared<LWFParticleRenderer>(lwf, particle, m_node);
}

void LWFRendererFactory::Init(LWF *lwf)
{
}

void LWFRendererFactory::BeginRender(LWF *lwf)
{
	m_maskMode = Format::BLEND_MODE_NORMAL;
	m_lastMaskMode = Format::BLEND_MODE_NORMAL;
	m_maskNo = -1;
}

void LWFRendererFactory::EndRender(LWF *lwf)
{
	if (!m_masks.empty()) {
		for (int i = ++m_maskNo; i < m_masks.size(); ++i) {
			auto& children = m_masks[i]->getChildren();
			for (auto& node : children)
				PlaceNode(m_node, node);
			m_masks[i]->removeFromParentAndCleanup(true);
		}
		m_masks.resize(m_maskNo);
	}
}

void LWFRendererFactory::Destruct()
{
	for (auto& mask : m_masks)
		mask->removeFromParentAndCleanup(true);
}

void LWFRendererFactory::FitForHeight(class LWF *lwf, float w, float h)
{
	ScaleForHeight(lwf, w, h);
	float offsetX = (w - lwf->width * lwf->scaleByStage) / 2.0f;
	float offsetY = -h;
	lwf->property->Move(offsetX, offsetY);
}

void LWFRendererFactory::FitForWidth(class LWF *lwf, float w, float h)
{
	ScaleForWidth(lwf, w, h);
	float offsetX = (w - lwf->width * lwf->scaleByStage) / 2.0f;
	float offsetY = -h + (h - lwf->height * lwf->scaleByStage) / 2.0f;
	lwf->property->Move(offsetX, offsetY);
}

void LWFRendererFactory::ScaleForHeight(class LWF *lwf, float w, float h)
{
	float scale = h / lwf->height;
	lwf->scaleByStage = scale;
	lwf->property->Scale(scale, scale);
}

void LWFRendererFactory::ScaleForWidth(class LWF *lwf, float w, float h)
{
	float scale = w / lwf->width;
	lwf->scaleByStage = scale;
	lwf->property->Scale(scale, scale);
}

bool LWFRendererFactory::Render(class LWF *lwf,
	Node *node, int renderingIndex, bool visible, BlendFunc *baseBlendFunc)
{
	m_renderingIndex = renderingIndex;

	node->setVisible(visible);
	if (!visible)
		return false;

	Node *target;
	switch (m_maskMode) {
	case Format::BLEND_MODE_ERASE:
	case Format::BLEND_MODE_MASK:
	case Format::BLEND_MODE_LAYER:
		{
			LWFMask *mask;
			if (m_lastMaskMode != m_maskMode) {
				++m_maskNo;
				if (m_masks.size() > m_maskNo) {
					mask = m_masks[m_maskNo];
				} else {
					mask = LWFMask::create();
					m_masks.push_back(mask);
				}
				PlaceNode(m_node, mask);

				LWFMask *layer = nullptr;
				if (m_lastMaskMode == Format::BLEND_MODE_LAYER &&
						(m_maskMode == Format::BLEND_MODE_ERASE ||
							m_maskMode == Format::BLEND_MODE_MASK) &&
						m_maskNo > 0) {
					layer = m_masks[m_maskNo - 1];
					if (layer)
						layer->setLocalZOrder(INT_MAX);
					PlaceNode(mask, layer);
				}

				if (layer && mask) {
					switch (m_maskMode) {
					case Format::BLEND_MODE_ERASE:
					case Format::BLEND_MODE_MASK:
						switch (m_maskMode) {
						case Format::BLEND_MODE_ERASE:
							layer->setBlendFunc(
								{GL_ONE_MINUS_DST_ALPHA, GL_ZERO});
							break;
						case Format::BLEND_MODE_MASK:
							layer->setBlendFunc(
								{GL_DST_ALPHA, GL_ZERO});
							break;
						}
						mask->setBlendFunc(
							{GL_ONE, GetBlendDstFactor(m_blendMode)});
						break;
					}
				}

				m_lastMaskMode = m_maskMode;
			} else {
				mask = m_masks[m_maskNo];
			}
			if (mask)
				mask->setLocalZOrder(m_renderingIndex + 1);
			target = mask;

		}
		break;

	default:
		target = m_node;
		break;
	}

	PlaceNode(target, node);

	node->setLocalZOrder(renderingIndex);

	BlendProtocol *p = dynamic_cast<BlendProtocol *>(node);
	if (!p)
		return true;

	cocos2d::BlendFunc blendFunc =
		baseBlendFunc ? *baseBlendFunc : p->getBlendFunc();
	blendFunc.dst = GetBlendDstFactor(m_blendMode);
	p->setBlendFunc(blendFunc);

	return true;
}

}	// namespace LWF
